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
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  struct proc* p;
  for(p = curproc->main_thread; p ; p= p->next_thread)
  {
    p->sz = sz;
  }
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

// int
// fork(void)
// {
//   int i, pid;
//   struct proc *np;
//   struct proc *curproc = myproc();

//   // Allocate process.
//   if((np = allocproc()) == 0){
//     return -1;
//   }

//   // Copy process state from proc.
//   if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
//     kfree(np->kstack);
//     np->kstack = 0;
//     np->state = UNUSED;
//     return -1;
//   }
//   np->sz = curproc->sz;
//   np->parent = curproc;
//   *np->tf = *curproc->tf;

//   // Clear %eax so that fork returns 0 in the child.
//   np->tf->eax = 0;

//   for(i = 0; i < NOFILE; i++)
//     if(curproc->ofile[i])
//       np->ofile[i] = filedup(curproc->ofile[i]);
//   np->cwd = idup(curproc->cwd);

//   safestrcpy(np->name, curproc->name, sizeof(curproc->name));

//   pid = np->pid;

//   acquire(&ptable.lock);

//   np->state = RUNNABLE;

//   release(&ptable.lock);

//   return pid;
// }

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
  }

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

// void
// exit(void)
// {
//   struct proc *curproc = myproc();
//   struct proc *p;
//   int fd;

//   if(curproc == initproc)
//     panic("init exiting");

//   // Close all open files.
//   for(fd = 0; fd < NOFILE; fd++){
//     if(curproc->ofile[fd]){
//       fileclose(curproc->ofile[fd]);
//       curproc->ofile[fd] = 0;
//     }
//   }

//   begin_op();
//   iput(curproc->cwd);
//   end_op();
//   curproc->cwd = 0;

//   acquire(&ptable.lock);

//   // Parent might be sleeping in wait().
//   wakeup1(curproc->parent);

//   // Pass abandoned children to init.
//   for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//     if(p->parent == curproc){
//       p->parent = initproc;
//       if(p->state == ZOMBIE)
//         wakeup1(initproc);
//     }
//   }

//   // Jump into the scheduler, never to return.
//   curproc->state = ZOMBIE;
//   sched();
//   panic("zombie exit");
// }

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

// int
// wait(void)
// {
//   struct proc *p;
//   int havekids, pid;
//   struct proc *curproc = myproc();
  
//   acquire(&ptable.lock);
//   for(;;){
//     // Scan through table looking for exited children.
//     havekids = 0;
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->parent != curproc)
//         continue;
//       havekids = 1;
//       if(p->state == ZOMBIE){
//         // Found one.
//         pid = p->pid;
//         kfree(p->kstack);
//         p->kstack = 0;
//         freevm(p->pgdir);
//         p->pid = 0;
//         p->parent = 0;
//         p->name[0] = 0;
//         p->killed = 0;
//         p->state = UNUSED;
//         release(&ptable.lock);
//         return pid;
//       }
//     }

//     // No point waiting if we don't have any children.
//     if(!havekids || curproc->killed){
//       release(&ptable.lock);
//       return -1;
//     }

//     // Wait for children to exit.  (See wakeup1 call in proc_exit.)
//     sleep(curproc, &ptable.lock);  //DOC: wait-sleep
//   }
// }

// PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
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
      
    struct proc *current_thread = p->main_thread->current_thread;

    do {
        current_thread = current_thread->next_thread;
        if (!current_thread)
            current_thread = p->main_thread;
    } while (current_thread->state != RUNNABLE);

    p->main_thread->current_thread = current_thread;

      c->proc = current_thread;
      switchuvm(current_thread);
      current_thread->state = RUNNING;
      // cprintf("tid: %d\n",current_thread->tid);
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

// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
  
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();

//     // Loop over process table looking for process to run.
//     acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;

//       // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;

//       swtch(&(c->scheduler), p->context);
//       switchkvm();

//       // Process is done running for now.
//       // It should have changed its p->state before coming back.
//       c->proc = 0;
//     }
//     release(&ptable.lock);

//   }
// }


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

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p -> main_thread ->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
    }
  }
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
    
    // Allocate process.
    if((np = allocproc()) == 0){
      return -1;
    }
    acquire(&ptable.lock);
    np->main_thread = mainThread;
    np->parent= mainThread->parent;
    np->pid = mainThread->pid; // thread shares PID with parent process
    *thread=++mainThread->create_num; // set the thread_t pointer to the new thread's TID
    np->tid = mainThread->create_num; 
    np->pgdir = curproc->pgdir; // thread shares page table with parent process
    // Clear %eax so that fork returns 0 in the child.

    // Allocate user stack for the thread.
    // mainThread->sz = PGROUNDUP(mainThread->sz);
    // acquire(&ptable.lock);

    sz=mainThread->sz;
    sz=PGROUNDUP(sz);
    newsz = allocuvm(curproc->pgdir, sz, sz + 2*PGSIZE);
    np->sz = newsz;
    mainThread->sz = newsz;
    clearpteu(curproc->pgdir, (char*)(newsz - 2*PGSIZE));
    // if((mainThread->sz = allocuvm(np->pgdir, mainThread->sz, mainThread->sz + 2*PGSIZE)) == 0){
    //     return -1;
    // }
    // clearpteu(np->pgdir, (char*)(mainThread->sz - 2*PGSIZE));
    // np->sz = mainThread->sz;
    // release(&ptable.lock);
    
   *np->tf = *curproc->tf;
  //  np->tf->eax = 0;
    np->tf->eip = (uint)start_routine; // set instruction pointer to start routine
    usp = newsz;
    usp -= 8;
    uint ustack[4];
    ustack[0] = 0xffffffff;
    ustack[1] = (uint)arg;

    // np->tf->esp -= 8; // move stack pointer to make room for the argument
    if(copyout(np->pgdir, usp, ustack, 8) < 0){
        np->state = UNUSED;
        return -1;
    } // push argument onto the stack
    np->tf->esp = (uint)usp;
    release(&ptable.lock);
    for(int i = 0; i < NOFILE; i++)
        if(curproc->ofile[i])
          np->ofile[i] = filedup(curproc->ofile[i]);
    np->cwd = idup(curproc->cwd);

    safestrcpy(np->name, curproc->name, sizeof(curproc->name));
    acquire(&ptable.lock);
    struct proc *p;
    for (p = curproc; p->next_thread != 0; p = p->next_thread);
    p->next_thread = np;
    np->next_thread = 0;

    np->state = RUNNABLE;
    np->is_thread = 1;
    np->join_thread = curproc;
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

  if (curproc->main_thread == curproc)
  {
    exit();
  }


  for(fd=0;fd<NOFILE;fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd]=0;
    }
  }
  
  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd=0;

  acquire(&ptable.lock);
  wakeup1(curproc->join_thread);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  curproc->state = ZOMBIE;
  curproc->retval = retval;
  sched();
  panic("zombie exit");

}
int thread_join(thread_t thread, void **retval) {
    struct proc *curproc = myproc();
    struct proc *mainThread = curproc->main_thread;
    int found = 0;

    acquire(&ptable.lock);

    // Find the thread to join
    struct proc *p;
    for (p = mainThread->next_thread; p ; p = p->next_thread) {
        if (p->tid == thread && p->join_thread == curproc) {
            found = 1;
            break;
        }
    }

    // If the thread is not found, return -1
    if (!found) {
        release(&ptable.lock);
        return -1;
    }

    // Wait until the thread exits
    while (p->state != ZOMBIE) {
        sleep(curproc, &ptable.lock);
    }

    // Set retval if requested
    // Retrieve the return value of the thread
    if (retval != 0) {
        *retval = p->retval;
    }
    if (p == mainThread->next_thread) {
        mainThread->next_thread = p->next_thread;
        if(p->main_thread->current_thread == p){
        p->main_thread->current_thread = mainThread;
    }} else {
        struct proc *prev;
        for (prev = mainThread->next_thread; prev->next_thread != p; prev = prev->next_thread)
        {
        prev->next_thread = p->next_thread;}
        if(p->main_thread->current_thread == p){
        p->main_thread->current_thread = prev;
    }
    }
    
    // Clean up the resources of the joined thread
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

    return 0;
}

void kill_for_exec(struct proc * curproc){
  struct proc *p;
  struct proc *next_thread;
  int fd;
  // acquire(&ptable.lock);
  for(p = curproc->main_thread; p; p= next_thread ){
    next_thread = p->next_thread;
    if(p != curproc){
      if(p->state !=ZOMBIE){
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
    }

    acquire(&ptable.lock);
    for(p = curproc->main_thread; p; p= next_thread ){
      next_thread = p->next_thread;
      if(p != curproc){
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
    }
  
  curproc->main_thread = curproc;
  curproc->is_thread = 0;
  curproc->tid = 0;
  curproc -> current_thread = curproc;
  curproc -> create_num = 0;
  curproc -> next_thread = 0;

  release(&ptable.lock);
}

