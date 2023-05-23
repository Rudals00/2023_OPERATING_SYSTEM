#include "types.h"
#include "stat.h"
#include "user.h"

#define MAX_CMD_LENGTH 256

// void print_process_info(struct proc *p) {
//   if (p->is_thread)
//     return; // Skip printing thread information
//   printf(1, "Name: %s\n", p->name);
//   printf(1, "PID: %d\n", p->pid);
//   printf(1, "Stack Pages: %d\n", p->main_thread->stack_pages);
//   printf(1, "Allocated Memory: %d bytes\n", p->sz);
//   printf(1, "Memory Limit: %d bytes\n", p->main_thread->memory_limit);
//   printf(1, "---------------------------\n");
// }


void tokenize(char *str, char **tokens, int max_tokens) {
  int i = 0;
  while (*str != '\0' && i < max_tokens - 1) {
    // Skip leading whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n')
      str++;
    if (*str == '\0')
      break;
    tokens[i++] = str;
    // Find the end of the token
    while (*str != '\0' && *str != ' ' && *str != '\t' && *str != '\n')
      str++;
    if (*str == '\0')
      break;
    *str++ = '\0'; // Null-terminate the token
  }
  tokens[i] = 0; // Null-terminate the token array
}

int main(int argc, char *argv[]) {
  char cmd[MAX_CMD_LENGTH];
  char process_name[16];
  int process_pid;
  int process_stack_pages;
  int process_allocated_memory;
  int process_memory_limit;

  while (1) {
    printf(1, "pmanager> ");
    gets(cmd, MAX_CMD_LENGTH);

    char *tokens[MAX_CMD_LENGTH / 2];
    tokenize(cmd, tokens, MAX_CMD_LENGTH / 2);

    if (tokens[0] == 0)
      continue; // Empty command, ask for another input

    if (strcmp(tokens[0], "list") == 0) {
      int i = 0;
      int flag;
      while (1) {
        flag = processinfo(i, process_name, &process_pid, &process_stack_pages, &process_allocated_memory, &process_memory_limit);
        i++;
        if (flag == -1)
          break;
        else if (flag == 0)
          continue;
        else {
          printf(1, "Name: %s\n", process_name);
          printf(1, "PID: %d\n", process_pid);
          printf(1, "Stack Pages: %d\n", process_stack_pages);
          printf(1, "Allocated Memory: %d bytes\n", process_allocated_memory);
          printf(1, "Memory Limit: %d bytes\n", process_memory_limit);
          printf(1, "---------------------------\n");
        
        }
      }
    }
    else if (strcmp(tokens[0], "kill") == 0) {
      char *pid_str = tokens[1];
      if (pid_str == 0) {
        printf(1, "Usage: kill <pid>\n");
        continue;
      }
      int pid = atoi(pid_str);
      if (kill(pid) == 0) {
        printf(1, "Process with PID %d killed successfully.\n", pid);
      } else {
        printf(1, "Failed to kill process with PID %d.\n", pid);
      }
    } else if (strcmp(tokens[0], "execute") == 0) {
      char *path = tokens[1];
      char *stacksize_str = tokens[2];
      char *val[2] = {path,0};
      if (path == 0 || stacksize_str == 0) {
        printf(1, "Usage: execute <path> <stacksize>\n");
        continue;
      }
      int stacksize = atoi(stacksize_str);
      int pid = fork();
      if(pid == 0){
        if (exec2(path, val, stacksize) < 0) {
          printf(1, "Failed to execute %s with stacksize %s.\n", path, stacksize_str);
        }
      }
     } else if (strcmp(tokens[0], "memlim") == 0) {
      char *pid_str = tokens[1];
      char *limit_str = tokens[2];
      if (pid_str == 0 || limit_str == 0) {
        printf(1, "Usage: memlim <pid> <limit>\n");
        continue;
      }
      int pid = atoi(pid_str);
      int limit = atoi(limit_str);
      if (setmemorylimit(pid, limit) == 0) {
        printf(1, "Memory limit set successfully for process with PID %d.\n", pid);
      } else {
        printf(1, "Failed to set memory limit for process with PID %d.\n", pid);
      }
    } else if (strcmp(tokens[0], "exit") == 0) {
      break; // Exit pmanager
    } else {
      printf(1, "Invalid command. Please try again.\n");
    }
  }

  exit();
}
// // void print_process_info(struct proc *p) {
// //   if (p->is_thread)
// //     return; // Skip printing thread information
// //   printf(1, "Name: %s\n", p->name);
// //   printf(1, "PID: %d\n", p->pid);
// //   printf(1, "Stack Pages: %d\n", p->main_thread->stack_pages);
// //   printf(1, "Allocated Memory: %d bytes\n", p->sz);
// //   printf(1, "Memory Limit: %d bytes\n", p->main_thread->memory_limit);
// //   printf(1, "---------------------------\n");
// // }
// void tokenize(char *str, char **tokens, int max_tokens) {
//   int i = 0;
//   while (*str != '\0' && i < max_tokens - 1) {
//     // Skip leading whitespace
//     while (*str == ' ' || *str == '\t' || *str == '\n')
//       str++;
//     if (*str == '\0')
//       break;
//     tokens[i++] = str;
//     // Find the end of the token
//     while (*str != '\0' && *str != ' ' && *str != '\t' && *str != '\n')
//       str++;
//     if (*str == '\0')
//       break;
//     *str++ = '\0'; // Null-terminate the token
//   }
//   tokens[i] = NULL; // Null-terminate the token array
// }

// int main(int argc, char *argv[]) {
//   char cmd[MAX_CMD_LENGTH];

//   while (1) {
//     printf(1, "pmanager> ");
//     gets(cmd, MAX_CMD_LENGTH);

//     char *token = strtok(cmd, " ");

//     if (token == 0)
//       continue; // Empty command, ask for another input

//     // if (strcmp(token, "list") == 0) {
//     //   struct proc *p;
//     //   acquire(&ptable.lock);
//     //   for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//     //     if (p->state != UNUSED ) {
//     //       print_process_info(p);
//     //     }
//     //   }
//     //   release(&ptable.lock);
//     if (strcmp(token, "kill") == 0) {
//       token = strtok(0, " ");
//       if (token == 0) {
//         printf(1, "Usage: kill <pid>\n");
//         continue;
//       }
//       int pid = atoi(token);
//       if (kill(pid) == 0) {
//         printf(1, "Process with PID %d killed successfully.\n", pid);
//       } else {
//         printf(1, "Failed to kill process with PID %d.\n", pid);
//       }
//     } else if (strcmp(token, "execute") == 0) {
//       char *path = strtok(0, " ");
//       char *stacksize = strtok(0, " ");
//       if (path == 0 || stacksize == 0) {
//         printf(1, "Usage: execute <path> <stacksize>\n");
//         continue;
//       }
//       int stack = atoi(stacksize);
//       if (exec2(path, argv, stack) < 0) {
//         printf(1, "Failed to execute %s with stacksize %s.\n", path, stacksize);
//       }
//     } else if (strcmp(token, "memlim") == 0) {
//       token = strtok(0, " ");
//       char *limit = strtok(0, " ");
//       if (token == 0 || limit == 0) {
//         printf(1, "Usage: memlim <pid> <limit>\n");
//         continue;
//       }
//       int pid = atoi(token);
//       int memlimit = atoi(limit);
//       if (setmemorylimit(pid, memlimit) == 0) {
//         printf(1, "Memory limit set successfully for process with PID %d.\n", pid);
//       } else {
//         printf(1, "Failed to set memory limit for process with PID %d.\n", pid);
//       }
//     } else if (strcmp(token, "exit") == 0) {
//     break; // Exit pmanager
//     } else {
//     printf(1, "Invalid command. Please try again.\n");
//     }
//     }

//     exit();
//     }

//     #include "types.h"
// #include "stat.h"
// #include "user.h"
// #include "fcntl.h"
// #include "fs.h"
// #include "proc.h"
// #include "param.h"
// #include "memlayout.h"

// void print_process_info(struct proc *p) {
//   if (p->is_thread)
//     return; // Skip printing thread information

//   printf(1, "Name: %s\n", p->name);
//   printf(1, "PID: %d\n", p->pid);
//   printf(1, "Stack Pages: %d\n", p->sz / PGSIZE);
//   printf(1, "Allocated Memory: %d\n", p->sz);
//   printf(1, "Memory Limit: %d\n", p->main_thread->memory_limit);
// }

// int main(void) {
//   char cmd[100];
//   while (1) {
//     printf(1, "pmanager> ");
//     gets(cmd, sizeof(cmd));
//     char *token = strtok(cmd, " ");
//     if (strcmp(token, "list") == 0) {
//       struct proc *p;
//       acquire(&ptable.lock);
//       for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
//         if (p->state != UNUSED ) {
//           print_process_info(p);
//         }
//       }
//       release(&ptable.lock);
//     } else if (strcmp(token, "kill") == 0) {
//       token = strtok(0, " ");
//       int pid = atoi(token);
//       if (kill(pid) == 0) {
//         printf(1, "Process with PID %d killed successfully.\n", pid);
//       } else {
//         printf(1, "Failed to kill process with PID %d.\n", pid);
//       }
//     } else if (strcmp(token, "execute") == 0) {
//       char *path = strtok(0, " ");
//       char *stacksize = strtok(0, " ");
//       int stack = atoi(stacksize);
//       char *argv[] = { path, 0 };
//       if (exec(path, argv) < 0) {
//         printf(1, "Failed to execute %s.\n", path);
//       }
//     } else if (strcmp(token, "memlim") == 0) {
//       token = strtok(0, " ");
//       int pid = atoi(token);
//       token = strtok(0, " ");
//       int limit = atoi(token);
//       if (setmemorylimit(pid, limit) == 0) {
//         printf(1, "Memory limit for process with PID %d set to %d.\n", pid, limit);
//       } else {
//         printf(1, "Failed to set memory limit for process with PID %d.\n", pid);
//       }
//     } else if (strcmp(token, "exit") == 0) {
//       break; // Exit pmanager
//     } else {
//       printf(1, "Invalid command. Please try again.\n");
//     }
//   }
//   exit();
// }