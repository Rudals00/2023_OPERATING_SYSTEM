#include "types.h"
#include "stat.h"
#include "user.h"
#define NULL ((void*)0)

void* increment(void* arg) {
    printf(1,"!!!");
    int i;
    for(i= (int) arg; i < 1000; i++) {
        printf(1, "inc : %d\n", i);
    }
    // printf(1,"arg = %d\n", (int)arg);
    exit();
    return NULL;
}

void* decrement(void* arg) {
    int i;
    for(i = (int) arg; i > 0; i--) {
        printf(1, "dec : %d\n", i);
    }
    return NULL;
}

thread_t thread;

int main(int argc, char *argv[])
{
    int i = 0;
    printf(1, "Thread test start\n");
    if(thread_create(&thread,increment, (void *)i)==0)
    {printf(1,"success");
    printf(1,"tid: %d", thread);}
    else printf(1,"@@");
    sleep(100000);
    
    exit();


    // for(i = 0; i < 5; i++) {
    //     thread_create(&thread[i],increment, (void *)i);
    // }
}