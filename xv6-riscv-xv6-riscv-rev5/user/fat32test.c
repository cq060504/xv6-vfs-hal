// fat32test: basic FAT32 mount/read/write/create/delete verification

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(void)
{
  int fd, n;
  char buf[64];
  struct stat st;

  printf("fat32test: starting\n");

  // ---- mount ----
  mkdir("/fat");
  if(mount("/fat", 3, "fat32") < 0){
    printf("FAIL: mount /fat 3 fat32\n");
    exit(1);
  }
  printf("PASS: mount\n");

  // ---- create + write ----
  fd = open("/fat/hello.txt", O_WRONLY | O_CREATE);
  if(fd < 0){
    printf("FAIL: create /fat/hello.txt\n");
    exit(1);
  }
  if(write(fd, "hello from fat32", 16) != 16){
    printf("FAIL: write\n");
    exit(1);
  }
  close(fd);
  printf("PASS: create + write\n");

  // ---- read back ----
  fd = open("/fat/hello.txt", O_RDONLY);
  if(fd < 0){
    printf("FAIL: open for read\n");
    exit(1);
  }
  n = read(fd, buf, sizeof(buf));
  if(n != 16 || memcmp(buf, "hello from fat32", 16) != 0){
    printf("FAIL: read mismatch (n=%d)\n", n);
    exit(1);
  }
  close(fd);
  printf("PASS: read\n");

  // ---- stat ----
  fd = open("/fat/hello.txt", O_RDONLY);
  if(fstat(fd, &st) < 0){
    printf("FAIL: stat\n");
    exit(1);
  }
  if(st.type != T_FILE){
    printf("FAIL: stat type=%d\n", st.type);
    exit(1);
  }
  close(fd);
  printf("PASS: stat (type=%d)\n", st.type);

  // ---- mkdir ----
  if(mkdir("/fat/subdir") < 0){
    printf("FAIL: mkdir\n");
    exit(1);
  }
  printf("PASS: mkdir\n");

  // ---- create file in subdir ----
  fd = open("/fat/subdir/nested.txt", O_WRONLY | O_CREATE);
  if(fd < 0){
    printf("FAIL: create nested\n");
    exit(1);
  }
  write(fd, "nested", 6);
  close(fd);
  printf("PASS: create nested\n");

  // ---- unlink ----
  if(unlink("/fat/hello.txt") < 0){
    printf("FAIL: unlink\n");
    exit(1);
  }
  fd = open("/fat/hello.txt", O_RDONLY);
  if(fd >= 0){
    printf("FAIL: unlink should have removed file\n");
    exit(1);
  }
  printf("PASS: unlink\n");

  // ---- cleanup ----
  unlink("/fat/subdir/nested.txt");
  unlink("/fat/subdir");
  umount("/fat");

  printf("fat32test: ALL PASSED\n");
  exit(0);
}
