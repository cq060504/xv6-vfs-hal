// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", CONSOLE, 0);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  // Prepare mount points and test files
  mkdir("/mnt");
  mkdir("/fat");
  int fd = open("/hello.c", O_WRONLY | O_CREATE);
  if(fd >= 0){ write(fd, "hello from xv6fs\n", 17); close(fd); }

  // Start a shell.  Manually run commands to test:
  //   usertests -q           — xv6 usertests
  //   ext2test2; ext2test1   — ext2 tests (order matters)
  //   ext2test3              — ext2 L4 test (may fail)
  //   fat32test              — fat32 test
  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      wpid = wait((int *) 0);
      if(wpid == pid){
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      }
    }
  }
}
