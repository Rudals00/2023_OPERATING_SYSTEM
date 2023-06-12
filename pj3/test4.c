#include "types.h"
#include "user.h"
#include "fcntl.h"

int
main(int argc, char *argv[])
{
  int fd, i;
  char data = 'a';

  fd = open("testfile", O_CREATE | O_WRONLY);
  if(fd < 0){
    printf(1, "test: cannot open testfile for writing\n");
    exit();
  }

  // Sync 호출 전 데이터 쓰기
  for(i = 0; i < 5; i++){
    write(fd, &data, sizeof(data));
  }

  // Sync 호출
  sync();

  // Sync 호출 후 추가 데이터 쓰기
  data = 'b';
  for(i = 0; i < 5; i++){
    write(fd, &data, sizeof(data));
  }

  close(fd);
  exit();
}