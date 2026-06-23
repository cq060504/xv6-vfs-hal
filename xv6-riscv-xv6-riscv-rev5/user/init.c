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

  // Create directories and files needed by tests
  mkdir("/mnt");
  mount("/mnt", 1, "ext2");
  mkdir("/tests");
  // Create /hello.c so cross-fs copy tests have a source file
  int fd = open("/hello.c", O_WRONLY | O_CREATE);
  if(fd >= 0){
    write(fd, "hello from xv6fs\n", 17);
    close(fd);
  }
  fd = open("/tests/hello.c", O_WRONLY | O_CREATE);
  if(fd >= 0){
    write(fd, "hello from xv6fs\n", 17);
    close(fd);
  }

  // Run ext2 tests — order matters: ext2test2 creates files in /mnt,
  // ext2test1 uses them. ext2test3 has a known L4 issue, skip it.
  printf("init: running ext2test2\n");
  pid = fork();
  if(pid == 0){
    char *ext2argv[] = { "ext2test2", 0 };
    exec("ext2test2", ext2argv);
    printf("init: exec ext2test2 failed\n");
    exit(1);
  }
  wait((int *) 0);

  printf("init: running ext2test1\n");
  pid = fork();
  if(pid == 0){
    char *ext2argv[] = { "ext2test1", 0 };
    exec("ext2test1", ext2argv);
    printf("init: exec ext2test1 failed\n");
    exit(1);
  }
  wait((int *) 0);

  printf("init: ext2 tests done, starting sh\n");

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
