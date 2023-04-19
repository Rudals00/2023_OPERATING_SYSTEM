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

int global_tick = 0;
int scheduler_locked=0;
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

void enqueue(struct proc *p, struct proc **queue) {
  if (p->already_enqueued) {
    return; // queue에 이미 process가 있는경우
  }
  if (*queue == 0) {
    // 큐가 비어있으면 프로세스를 큐의 첫 번째 요소로 추가
    *queue = p;
    p->next = 0;
    p->already_enqueued = 1;
  } else {
    struct proc *tmp = *queue;
    // 큐의 마지막 요소를 찾음
    while (tmp->next) {
      tmp = tmp->next;
    }

    // 마지막 요소 다음에 프로세스를 추가
    tmp->next = p;
    p->next = 0;
    p->already_enqueued = 1;

  }
}

struct proc *dequeue(struct proc **queue) {
  if (*queue == 0) {
    // 큐가 비어있으면 0 반환
    return 0;
  } else {
    struct proc *dequeued_proc = *queue;
    // 큐의 첫 번째 요소를 제거하고 두 번째 요소를 첫 번째 요소로 설정
    *queue = (*queue)->next;
    dequeued_proc->next = 0;

    dequeued_proc->already_enqueued = 0;
    return dequeued_proc;
  }
}

void
dequeue_specific(struct proc **queue, struct proc *p_specific)
{
  struct proc *p = 0;
  struct proc *prev = 0;

  if(p_specific->already_enqueued == 0){
    return;
  }//p가 큐에 들어가 있지 않은 상태
  
  for (p = *queue; p != 0; prev = p, p = p->next) {
    if (p == p_specific) {
      if (prev) {
        prev->next = p->next;
      } else { //찾고자 하는 값이 첫번째 인경우
        *queue = p->next;
      }
      p->next = 0;
      p->already_enqueued = 0;
      break;
    }
  }
}

void
schedulerLock(int password)
{
  if(scheduler_locked == 1){
    cprintf("already locked\n");
    return;
  } //이미 Lock이 걸려있는 경우

  if(password == 2019041703){
    global_tick = 0;
    scheduler_locked = 1;
    myproc()->lock_scheduler = 1;
  }
  else{
    cprintf("Incorrect password for schedulerLock\n");
    cprintf("Pid : %d , time quantum : %d, Queue_Level : %d\n",myproc()->pid,myproc()->time_allotment, myproc()->level);
    exit();    
  }
}

void
schedulerUnlock(int password){
  if(scheduler_locked!=1){
    cprintf("Scheduler is not locked.");
    return;
  } //Lock이 걸려있지 않은 경우

  if(password == 2019041703){
    struct proc *p = 0;
    // Lock이 걸려있는 process를 찾는다
    for (int level = 0; level < 3 && p == 0; level++) {
      for (struct proc *current = level_queue[level]; current != 0; current = current->next) {
        if (current->lock_scheduler == 1) {
          p = current; 
          break;
        }
      }
    }

    // process가 있는경우
    if (p != 0) {
      scheduler_locked = 0;
      p->lock_scheduler = 0;
      
      if(p->state !=RUNNABLE){
         dequeue_specific(&level_queue[p->level],p);
      } //Runnable이 아니라면 Queue에서 제거
      if(p->state == RUNNABLE|| p->state==RUNNING){
        p->next = level_queue[0];
        level_queue[0] = p;
        p->level = 0;
        p->time_allotment = 0;
        p->time_quantum = 2 * p->level + 4;
        p->already_enqueued = 1; 
      } //RUNNABLE 또는 Running이면 LO의 Head에 넣어주기
    }
  }
    else {
    cprintf("Incorrect password for schedulerUnLock\n");
    cprintf("Pid : %d , time quantum : %d, Queue_Level : %d\n", myproc()->pid, myproc()->time_allotment, myproc()->level);
    exit();
  }
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

  //mlfq 관련 초기화
  p->level = 0;
  p->priority = 3;
  p->time_quantum = 2 * p->level +4;
  p->time_allotment = 0;
  p->next = 0;

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
  enqueue(p, &level_queue[p->level]);

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
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
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  enqueue(np, &level_queue[np->level]);

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
        freevm(p->pgdir);
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

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
const char* state_to_string(enum procstate state) {
  switch (state) {
    case UNUSED:
      return "UNUSED";
    case EMBRYO:
      return "EMBRYO";
    case SLEEPING:
      return "SLEEPING";
    case RUNNABLE:
      return "RUNNABLE";
    case RUNNING:
      return "RUNNING";
    case ZOMBIE:
      return "ZOMBIE";
    default:
      return "UNKNOWN";
  }
}

void
scheduler(void)
{
  struct proc *p = 0;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    
    // Loop over process table looking for process to run.
  acquire(&ptable.lock);
  if(p&&scheduler_locked==1){
    //락이 걸려있으면 dequeue를 해주지 않고 기존의 p값을 그대로 가져감
  }
  else{
    for(int level = 0; level < 3; level++) {
      struct proc *p_high_priority = 0;
      //level2에서 우선적으로 실행되야 할 process 선택
      if (level == 2) {
        for (p = level_queue[level]; p != 0; p = p->next) {
          if (p_high_priority == 0 || p->priority < p_high_priority->priority) {
            p_high_priority = p;
          }
        }
        if (p_high_priority != 0) {
          dequeue_specific(&level_queue[level], p_high_priority);
          p = p_high_priority;
          break;
        }
      } else {
        p = dequeue(&level_queue[level]);
        if (p != 0) {
          break;
        }
      }
    }
  }

  if(p == 0) {
    release(&ptable.lock);
    continue;
  }
    
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    
    // cprintf("pid: %d time_allotment: %d priority: %d, level : %d , globaltick : %d is_lock : %d ptate: %s proc_lock:%d enqueued: %d \n",p->pid,p->time_allotment,p->priority, p->level, global_tick,scheduler_locked,state_to_string(p->state),p->lock_scheduler, p->already_enqueued);

    swtch(&(c->scheduler), p->context);
    switchkvm();
    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;

    if (scheduler_locked == 1 && p->state != RUNNABLE) {
      //scheduler_lockde이 되어있는데 p가 RUNNABLE이 아니라면 lock을 해제함
      schedulerUnlock(2019041703);
    }
    // 레벨 변경이 필요한 경우
    if (p->level < 2 && p->time_allotment >= p->time_quantum) 
    {//Level 0과 1에서 timequantum 을 다 사용한 경우
      if(scheduler_locked==1)
      {//lock이 걸려있으면 우선 queue에서 빼줌
        dequeue_specific(&level_queue[p->level],p);
      }
      p->level++;
      p->time_quantum = 2 * p->level + 4;
      p->time_allotment = 0;
    } 
    else if (p->level == 2 && p->time_allotment >= p->time_quantum) 
    {//leve2에서 timequantum을 다 사용한 경우 priority 변경
      p->priority = (p->priority - 1 >= 0) ? p->priority - 1 : 0;
      p->time_allotment = 0;
    }
    if(p->state == RUNNABLE) {
      // cprintf("@@\n");
      enqueue(p, &level_queue[p->level]);
    }
    if(scheduler_locked!=1)
    {//schedulerlock이 걸려있지 않다면 p초기화
    // cprintf("kkk\n");
      p = 0;
    }
    //priority boosting이 필요한경우
    if (global_tick >= 100) 
    {
      struct proc *current = level_queue[0];

      // level_queue[0]를 모두 초기화하고 끝으로 이동
      while (current != 0 && current->next != 0) {
        current->priority = 3;
        current->time_allotment = 0;
        current = current->next;
      }
      if (current != 0) {
        //level_queue[0]의 맨 마지막 값 초기화
        current->priority = 3;
        
        current->time_allotment = 0;
      }

      // level_queue[1] 및 level_queue[2]의 프로세스를 level_queue[0]로 이동
      for (int level = 1; level <= 2; level++) {
        struct proc *next_level_process = level_queue[level];

        while (next_level_process != 0) {
          next_level_process->level = 0;
          next_level_process->priority = 3; 
          next_level_process->time_allotment = 0;
          next_level_process->time_quantum = 2 * next_level_process->level + 4;

          if (current == 0) {
            //leve_queue[0]이 비어있는 경우
            level_queue[0] = next_level_process;
            current = next_level_process;
          } 
          else {
            current->next = next_level_process;
            current = next_level_process;
          }
          next_level_process = next_level_process->next;
        }
        level_queue[level] = 0; // 현재 레벨 큐를 초기화
      }
      if(scheduler_locked==1){
        //schedulerlock이 걸려있으면 해제
        schedulerUnlock(2019041703);
      }
      global_tick = 0; // global_tick초기화
    }
    release(&ptable.lock);
  }
}

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

int
getLevel(void)
{ 
  return myproc()->level;
}
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&ptable.lock);
  p->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

void setPriority(int pid, int priority) {
  if (priority < 0 || priority > 3) {
    cprintf("Invalid priority value. Priority must be between 0 and 3.\n");
    return;
  }// priority 범위 밖의 값이 인자로 들어왔을 경우

  struct proc *p;
  int pid_found = 0;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->pid == pid) {
      p->priority = priority;
      pid_found = 1;
      break;
    } // 해당 pid값의 process를 찾고 priority 값 변경 후 pid_fount flag 값 변경
  }
  release(&ptable.lock);

  if (!pid_found) {
    cprintf("PID not found.\n");
  }
  return;
}



//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      enqueue(p, &level_queue[p->level]);
    }

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
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
        p->state = RUNNABLE;
        enqueue(p, &level_queue[p->level]);
      }
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


