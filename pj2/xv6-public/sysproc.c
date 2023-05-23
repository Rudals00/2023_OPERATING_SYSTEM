#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// int
// sys_thread_create(void)
// {
//   thread_t *thread;
//   void *(*start_routine)(void*);
//   void *arg;

//   // getting the arguments passed from user space
//   if(argptr(0, (void*)&thread, sizeof(thread_t)) < 0 ||
//      argptr(1, (void*)&start_routine, sizeof(start_routine)) < 0 ||
//      argptr(2, (void*)&arg, sizeof(arg)) < 0)
//     return -1;

//   return thread_create(thread, start_routine, arg);
// }

int
sys_thread_create(void)
{
  thread_t *thread;
  void *(*start_routine)(void *);
  void *arg;
  if(argptr(0,(char **)&thread,sizeof(thread)) < 0)
    return -1;
  if(argptr(1,(char **)&start_routine,sizeof(start_routine)) < 0)
    return -1;
  if(argptr(2,(char **)&arg,sizeof(arg)) < 0)
    return -1;
  return thread_create(thread,start_routine,arg);
}

int
sys_thread_exit(void)
{
  void *retval;
  if(argptr(0,(char **)&retval,sizeof(retval)) < 0)
    return -1;
  thread_exit(retval);
  return 0;
}

int
sys_thread_join(void)
{
  thread_t thread;
  void **retval;
  if(argint(0,&thread) < 0)
    return -1;
  if(argptr(1,(char **)&retval,sizeof(retval)) < 0)
    return -1;
  return thread_join(thread,retval);
}

int sys_setmemorylimit(void) {
  int pid, limit;
  if (argint(0, &pid) < 0 || argint(1, &limit) < 0) {
    return -1;
  }
  return setmemorylimit(pid, limit);
}

int
sys_exec2(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;
  int stacksize;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0|| argint(2, &stacksize)<0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec2(path, argv,stacksize);
}


int sys_processinfo(void) {
int index;
char *process_name;
int *process_pid, *process_stack_pages, *process_allocated_memory, *process_memory_limit;

if (argint(0, &index) < 0 || argptr(1, (void *)&process_name, sizeof(char *)) < 0 ||
argptr(2, (void *)&process_pid, sizeof(int *)) < 0 ||
argptr(3, (void *)&process_stack_pages, sizeof(int *)) < 0 ||
argptr(4, (void *)&process_allocated_memory, sizeof(int *)) < 0 ||
argptr(5, (void *)&process_memory_limit, sizeof(int *)) < 0) {
return -1;
}

int result = processinfo(index, process_name, process_pid, process_stack_pages, process_allocated_memory, process_memory_limit);
return result;
}