#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  //thread 관련 초기화
  p->is_thread = 0;
  p->tid = 0;
  p->create_num = 0;
  p->current_thread = p;
  p->main_thread = p;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  return p;
}


//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{ 
  uint sz;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  sz = curproc->main_thread->sz;
  if(n > 0){
    uint new_sz = sz + n;
    uint memory_limit = curproc->main_thread->memory_limit;
    if (memory_limit > 0 && new_sz > memory_limit) {
      release(&ptable.lock);
      return -1;  // 메모리 limit 확인후 에러발생시 -1 return
    }
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0){
      release(&ptable.lock);
      return -1;
    }
  }
  struct proc* p;
  for(p = curproc->main_thread; p ; p= p->next_thread)
  {
    p->sz = sz;
  }//변경된 sz값 해당 프로세스의 thread에 모두 적용
  release(&ptable.lock);
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->main_thread->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->main_thread->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->main_thread->name, sizeof(curproc->main_thread->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}


// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  for(p = curproc->main_thread; p; p = p->next_thread) {
    if(p != curproc &&p->state != ZOMBIE) {
      for(fd = 0; fd < NOFILE; fd++) {
        if(p->ofile[fd]) {
          fileclose(p->ofile[fd]);
          p->ofile[fd] = 0;
        }
      }
      
      begin_op();
      iput(p->cwd);
      end_op();
      p->cwd = 0;
    }
  }
  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;
  
  acquire(&ptable.lock);

  
  struct proc *next_thread;
  for(p = curproc->main_thread; p; p = next_thread){
    next_thread = p->next_thread;
    if(p->pid == curproc->pid && p->tid != curproc->tid){
    kfree(p->kstack);
    p->kstack = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->state = UNUSED;
    p->tid = 0;
    p->join_thread = 0;
    p->main_thread = 0;
    p->pid = 0;
    p->next_thread = 0;
    p->is_thread = 0;
    }
  }//exit을 호출한 process내의 모든 thread의 자원할당을 해제

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}


// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->main_thread->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
        continue;
      
    struct proc *current_thread = p->main_thread->current_thread;//현재 프로세스의 이전까지 실행했던 thread 가져옴
    
    //실행가능한 다음 thread찾기
    do {
        current_thread = current_thread->next_thread;
        if (!current_thread)
            current_thread = p->main_thread;
    } while (current_thread->state != RUNNABLE);

    p->main_thread->current_thread = current_thread;//current thread 업데이트

      c->proc = current_thread;
      switchuvm(current_thread);
      current_thread->state = RUNNING;
      swtch(&(c->scheduler), current_thread->context);
      switchkvm();
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;

      // Move to the next thread or loop back to the main thread
    }
    release(&ptable.lock);
  }
}


// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.


static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);

  // for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
  //   if(p -> main_thread ->pid == pid){
  //     p->killed = 1;
  //     if(p->state == SLEEPING)
  //       p->state = RUNNABLE;
  //   }
  // }
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int thread_create(thread_t *thread, void *(*start_routine)(void*), void *arg) {
    struct proc *np;
    struct proc *curproc = myproc();
    struct proc *mainThread=curproc->main_thread;
    uint usp,sz,newsz;
    
    // 프로세스 할당
    if((np = allocproc()) == 0){
      return -1;
    }
    acquire(&ptable.lock);
    np->main_thread = mainThread; //mainthread 지정
    np->parent= mainThread->parent; //부모 프로세스 지정
    np->pid = mainThread->pid; // mainthread와 pid공유
    *thread=++mainThread->create_num; // thread 번호 저장
    np->tid = mainThread->create_num; // thread 번호 tid에 저장
    np->pgdir = curproc->pgdir; // pgdir 복사
    // Clear %eax so that fork returns 0 in the child.

    // thread 스택 공간 확보
    sz=mainThread->sz; 
    sz=PGROUNDUP(sz);
    newsz = allocuvm(curproc->pgdir, sz, sz + 2*PGSIZE);

    // 메모리 limit check
    uint memory_limit = mainThread->memory_limit;
    if (memory_limit > 0 && newsz > memory_limit) {
      release(&ptable.lock);
      return -1;  // 메모리 제한 초과시, 오류 반환
    }

    np->sz = newsz;
    mainThread->sz = newsz;
    clearpteu(curproc->pgdir, (char*)(newsz - 2*PGSIZE));
    
    //유저 스택 값 넣기
   *np->tf = *curproc->tf;
    np->tf->eip = (uint)start_routine; // start routine 지정
    usp = newsz;
    usp -= 8;
    uint ustack[4];
    ustack[0] = 0xffffffff;
    ustack[1] = (uint)arg;

    // 스택 포인터 이동하여 인자공간 확보
    if(copyout(np->pgdir, usp, ustack, 8) < 0){
        np->state = UNUSED;
        return -1;
    } // 스택에 인자 입력
    np->tf->esp = (uint)usp;

    // 파일 복사
    for(int i = 0; i < NOFILE; i++)
        if(curproc->ofile[i])
          np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));


    //생성된 thread linkedlist에 달아주기
    struct proc *p;
    for (p = curproc; p->next_thread != 0; p = p->next_thread);
    p->next_thread = np;
    np->next_thread = 0;

    np->state = RUNNABLE;
    np->is_thread = 1;
    np->join_thread = curproc; //join을 위한 현재 프로세스 정보 저장
    //프로세스 내의 sz 복사
    for(p = curproc->main_thread; p ; p= p->next_thread)
    {
      p->sz = newsz;
    }
    release(&ptable.lock);
    return 0;
}

void thread_exit(void *retval){
  struct proc * curproc=myproc();
  struct proc * p;
  int fd;
  // 만약 현재 프로세스가 메인 스레드라면, exit를 호출
  if (curproc->main_thread == curproc)
  {
    exit();
  }

  // 현재 프로세스에서 열려있는 모든 파일을 닫음
  for(fd=0;fd<NOFILE;fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd]=0;
    }
  }
  
  // 작업 시작
  begin_op();
  // 현재 프로세스의 작업 디렉토리를 닫음
  iput(curproc->cwd);
  // 작업 종료
  end_op();
  curproc->cwd=0;

  // 프로세스 테이블 잠금
  acquire(&ptable.lock);
  // join을 기다리는 스레드 깨우기
  wakeup1(curproc->join_thread); 
  // 모든 자식 프로세스를 확인해서 부모 프로세스를 initproc으로 변경하고, 상태가 ZOMBIE라면 initproc 깨우기
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  // 현재 프로세스 상태를 ZOMBIE로 변경
  curproc->state = ZOMBIE;
  // 반환값 설정
  curproc->retval = retval;
  // 스케줄러 호출
  sched();
  // 이 위치에 도달했다면 문제가 있는 것이므로 panic 호출
  panic("zombie exit");
  }

int thread_join(thread_t thread, void **retval) {
    struct proc *curproc = myproc();
    struct proc *mainThread = curproc->main_thread;
    int found = 0;

    acquire(&ptable.lock);

    // join 할 thread 찾기
    struct proc *p;
    for (p = mainThread->next_thread; p ; p = p->next_thread) {
        if (p->tid == thread && p->join_thread == curproc) {
            found = 1;
            break;
        }
    }

    // thread를 찾지 못하면 -1 반환
    if (!found) {
        release(&ptable.lock);
        return -1;
    }

    // thread가 종료될 때까지 기다림
    while (p->state != ZOMBIE) {
        sleep(curproc, &ptable.lock);
    }

    // 요청된 경우 retval 설정
    // thread의 반환 값을 가져옴
    if (retval != 0) {
        *retval = p->retval;
    }

    // thread 리스트에서 제거
    if (p == mainThread->next_thread) {
        mainThread->next_thread = p->next_thread;
        if(p->main_thread->current_thread == p){
            p->main_thread->current_thread = mainThread;
        } 
    } else {
        struct proc *prev;
        for (prev = mainThread->next_thread; prev->next_thread != p; prev = prev->next_thread)
            prev->next_thread = p->next_thread;
        if(p->main_thread->current_thread == p){
            p->main_thread->current_thread = prev;
        }
    }
    
    // join된 thread의 자원회수
    kfree(p->kstack);
    p->kstack = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->state = UNUSED;
    p->tid = 0;
    p->join_thread = 0;
    p->main_thread = 0;
    p->pid = 0;
    p->next_thread = 0;
    p->is_thread = 0;

    release(&ptable.lock);

    return 0; // 성공적으로 thread join 완료
}

// 현재 프로세스(curproc)에 대해 exec를 위한 종료 과정을 수행하는 함수
void 
kill_for_exec(struct proc * curproc){
  struct proc *p;
  struct proc *next_thread;
  int fd;

  for(p = curproc->main_thread; p; p= next_thread ){
    next_thread = p->next_thread;

    // 현재 스레드가 현재 프로세스가 아닌 경우
    if(p != curproc){

      // 스레드 상태가 ZOMBIE가 아닌 경우
      if(p->state !=ZOMBIE){

        // 열려있는 파일을 모두 닫는다
        for(fd = 0; fd < NOFILE; fd++) {
          if(p->ofile[fd]) {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
          }
        }

        // 현재 작업 디렉토리를 해제
        begin_op();
        iput(p->cwd);
        end_op();
        p->cwd = 0;
      }
    }
  }

  acquire(&ptable.lock);

  // 다시 한번 현재 프로세스의 메인 스레드를 순회하며
  for(p = curproc->main_thread; p; p= next_thread ){
    next_thread = p->next_thread;

    // 현재 스레드가 현재 프로세스가 아닌 경우
    if(p != curproc){

      // 프로세스의 커널 스택을 해제
      kfree(p->kstack);
      p->kstack = 0;

      // 프로세스 정보를 초기화
      p->parent = 0;
      p->name[0] = 0;
      p->state = UNUSED;
      p->tid = 0;
      p->join_thread = 0;
      p->main_thread = 0;
      p->pid = 0;
      p->next_thread = 0;
      p->is_thread = 0;
    }
  }
  
  // 현재 프로세스 정보를 업데이트
  curproc->main_thread = curproc;
  curproc->is_thread = 0;
  curproc->tid = 0;
  curproc -> current_thread = curproc;
  curproc -> create_num = 0;
  curproc -> next_thread = 0;

  release(&ptable.lock);
}

// 프로세스의 메모리 제한을 설정하는 함수
int setmemorylimit(int pid, int limit) {
  struct proc *p;
  if(limit < 0) {
    return -1;} // 제한 값이 유효한지 확인
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    
    // 해당 pid를 가진 프로세스를 찾고, thread가 아닌 경우에만 메모리 제한을 설정
    if (p->pid == pid && p->is_thread == 0) {

      // 프로세스가 존재하는지 확인
      if (p->state == UNUSED)
      {
        release(&ptable.lock);
        return -1;
      }

      // 이미 프로세스가 새로 설정할 제한값보다 많은 메모리를 사용하고 있는지 확인
      if (limit < p->sz)
      {
        release(&ptable.lock);
        return -1;
      }

      // 메모리 limit 업데이트
      p->main_thread->memory_limit = limit;
      release(&ptable.lock);
      return 0;
    }
  }
  // 해당 pid를 가진 프로세스를 찾지 못한 경우, -1 반환
  release(&ptable.lock);
  return -1;
}

int processinfo(int index, char *process_name, int *process_pid, int *process_stack_pages, int *process_allocated_memory, int *process_memory_limit ) {
  if(index>=NPROC) return -1;
  struct proc *p;
  acquire(&ptable.lock);
  p=ptable.proc;
  while(index>0){
    p++;
    index--;
  }
  if(p->is_thread&&p) return 0;
  else if(p->state !=UNUSED && p->killed == 0){
    safestrcpy(process_name, p->name, strlen(p->name) + 1);
// Copy process PID
    *process_pid = p->pid;
    int stack_pages = p->sz / PGSIZE;
    *process_stack_pages = stack_pages;
    *process_allocated_memory = p->sz;
    *process_memory_limit = p->main_thread->memory_limit;
    release(&ptable.lock);
    return 1;
  }
  release(&ptable.lock);
  return -1;
}