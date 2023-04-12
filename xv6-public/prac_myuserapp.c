// test
#include "types.h"
#include "user.h"
#include "fcntl.h"
#define NUM_CHILD 5
#define NUM_LOOP1 500000
#define NUM_LOOP2 1000000
#define NUM_LOOP3 200000
#define NUM_LOOP4 1000000

int me;
int create_child(void){
  for(int i =0  ; i<NUM_CHILD; i++){
    int pid = fork();
    if(pid == 0){
      me = i;
      sleep(10);
      return 0;
    }
  }
  return 1;
}

void exit_child(int parent) {
	if (parent)
		while (wait() != -1); // wait for all child processes to finish
	exit();
}

int main()
{
  int p;
  p = create_child();

	if (!p) {
		int cnt[3] = {0, };
		for (int i = 0; i < NUM_LOOP1; i++) {
			// cnt[getLevel()]++;
		}
		printf(1, "process : L0=%d, L1=%d, L2=%d\n", cnt[0], cnt[1],cnt[2]);
	}
	exit_child(p);
}
