#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define N 100
#define SIZE 1024
#define FSIZE 16 * 1024 * 1024
struct test {
    char file[SIZE];
};
char buf[512];

void
save(char* filename)
{
    int fd;
    struct test t;
    for (int i = 0; i < SIZE; i++) t.file[i] = '1';
    fd = open(filename, O_CREATE | O_RDWR);
    if(fd >= 0) {
        printf(1, "Create Success\n");
    } else {
        printf(1, "Error: Create failed\n");
        exit();
    }

    int size = sizeof(t);
    printf(1, "[%d]", size);
    for (int i = 0; i < 1024; i++){
            for (int j = 0; j < 16; j++){
            if(write(fd, &t, size) != size){
                printf(1, "Error: Write failed\n");
                exit();
            }
        }
    }
    printf(1, "write ok\n");
    close(fd);
}

void
load(char* filename)
{
    int fd;
    struct test t;
    fd = open(filename, O_RDONLY);
    if(fd >= 0) {
        printf(1, "Open Success\n");
    } else {
        printf(1, "Error: open failed\n");
        exit();
    }

    int size = sizeof(t);
    if(read(fd, &t, size) != size){
        printf(1, "Error: read failed\n");
        exit();
    }
    printf(1, "Read Success\n");
    close(fd);
}
void
printFile(int fd, char *name, int line)
{
    int i, n; //here the size of the read chunk is defined by n, and i is used to keep a track of the chunk index
    int l, c; // here line number is defined by l, and the character count in the string is defined by c

    l = c = 0;

  while((n = read(fd, buf, sizeof(buf))) > 0 )
  {
    for(i=0;i<=n ;i++)
    {            
        //print the characters in the line 
        if(buf[i]!='\n'){         
            printf(1,"%c",buf[i]);
        }   
        //if the number of lines is equal to l, then exit
        else if (l == (line-1)){
        printf(1,"\n");   
        exit();
        }
        //if the number of lines is not equal to l, then jump to next line and increment the value of l 
        else{
            printf(1,"\n");
            l++;
        } 
    }
  }

  if(n < 0){
    printf(1, "printFile: read error\n");
    exit();
  }
}

int
main(void)
{
    //exit();
    char filename[5] = "test";
    char n = '0';
    for (int i = 0; i < 4; i++){
    n = i + '0';
    filename[4] = n;
    save(filename);
    //printFile(fd, filename, 10);
    load(filename);
    if (unlink(filename)<0) printf(1, "unlink fail");
    }
    exit();
}