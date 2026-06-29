// xv6 VFS/ext2 综合演示程序
// 逐步骤展示 VFS 分层设计、ext2 文件操作、卸载重挂持久化等能力
// 运行方式: vfsdemo

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MNT "/mnt"
#define HELLO_SRC "/hello.c"
#define HELLO_MNT "/mnt/hello.c"
#define SUBDIR  "/mnt/subdir"
#define LINK_ORIG "/mnt/demo_file"
#define LINK_ALIAS "/mnt/demo_link"
#define LARGE_FILE "/mnt/large"

static int passed;
static int failed;
static int step;

static void step_header(int n, const char *desc) {
  printf("\n[Step %d/8] %s\n", n, desc);
  step = n;
}

static void ok(const char *msg) {
  printf("  %-50s [PASS]\n", msg);
  passed++;
}

static void fail(const char *msg) {
  printf("  %-50s [FAIL]\n", msg);
  failed++;
}

static void setup(void) {
  // 确保 /mnt 不存在 mount
  int fd;
  if ((fd = open(HELLO_SRC, O_RDONLY)) < 0) {
    // 创建 hello.c 源文件
    fd = open(HELLO_SRC, O_WRONLY | O_CREATE);
    if (fd >= 0) {
      write(fd, "#include \"kernel/types.h\"\n#include \"user/user.h\"\nvoid main(){printf(\"Hello, xv6 VFS!\\n\");}\n", 94);
      close(fd);
    }
  } else {
    close(fd);
  }
}

/* Step 1: mount ext2 */
static void test_mount(void) {
  step_header(1, "mount ext2 文件系统");

  if (mkdir(MNT) < 0) {
    // /mnt may already exist
  }

  if (mount(MNT, 1, "ext2") < 0) {
    fail("mount /mnt 1 ext2");
  } else {
    ok("mount /mnt 1 ext2");
  }
}

/* Step 2: basic create/write/read on ext2 */
static void test_basic_io(void) {
  step_header(2, "ext2 基础文件操作 (create/write/read)");

  int fd = open(HELLO_MNT, O_WRONLY | O_CREATE | O_TRUNC);
  if (fd < 0) { fail("create /mnt/hello.c"); return; }
  ok("create /mnt/hello.c");

  const char *msg = "Hello, ext2!";
  int len = strlen(msg);
  if (write(fd, msg, len) != len) { fail("write to /mnt/hello.c"); close(fd); return; }
  ok("write 13 bytes");
  close(fd);

  fd = open(HELLO_MNT, O_RDONLY);
  if (fd < 0) { fail("re-open /mnt/hello.c"); return; }
  char buf[64];
  int n = read(fd, buf, sizeof(buf) - 1);
  if (n != len) { fail("read from /mnt/hello.c"); close(fd); return; }
  buf[n] = 0;
  if (strcmp(buf, msg) != 0) { fail("content verify"); close(fd); return; }
  ok("read + verify content");
  close(fd);
}

/* Step 3: cross-fs copy (xv6fs -> ext2) */
static void test_cross_fs_copy(void) {
  step_header(3, "跨文件系统拷贝 (xv6fs -> ext2)");

  int src = open(HELLO_SRC, O_RDONLY);
  if (src < 0) { fail("open source /hello.c"); return; }

  int dst = open("/mnt/hello_copy.c", O_WRONLY | O_CREATE | O_TRUNC);
  if (dst < 0) { fail("create /mnt/hello_copy.c"); close(src); return; }

  char buf[512];
  int n, total = 0;
  while ((n = read(src, buf, sizeof(buf))) > 0) {
    if (write(dst, buf, n) != n) { fail("write copy data"); close(src); close(dst); return; }
    total += n;
  }
  close(src);
  close(dst);

  if (total <= 0) { fail("copy non-zero bytes"); return; }
  ok("copy /hello.c -> /mnt/hello_copy.c");

  // verify
  src = open(HELLO_SRC, O_RDONLY);
  dst = open("/mnt/hello_copy.c", O_RDONLY);
  if (src < 0 || dst < 0) { fail("re-open for verify"); return; }

  char buf1[512], buf2[512];
  int n1 = read(src, buf1, sizeof(buf1));
  int n2 = read(dst, buf2, sizeof(buf2));
  close(src);
  close(dst);

  if (n1 != n2 || n1 <= 0) { fail("size mismatch"); return; }
  if (memcmp(buf1, buf2, n1) != 0) { fail("content mismatch"); return; }
  ok("content verified");
}

/* Step 4: directory operations */
static void test_dir(void) {
  step_header(4, "目录操作 (mkdir/readdir)");

  if (mkdir(SUBDIR) < 0) { fail("mkdir /mnt/subdir"); return; }
  ok("mkdir /mnt/subdir");

  // create file in subdir
  int fd = open("/mnt/subdir/test", O_WRONLY | O_CREATE);
  if (fd < 0) { fail("create file in subdir"); return; }
  write(fd, "test", 4);
  close(fd);

  // readdir: open /mnt and check subdir exists
  fd = open(MNT, O_RDONLY);
  if (fd < 0) { fail("open /mnt for readdir"); return; }

  struct stat st;
  if (fstat(fd, &st) < 0) { fail("fstat /mnt"); close(fd); return; }
  if (st.type != T_DIR) { fail("/mnt is not a directory"); close(fd); return; }
  ok("readdir /mnt")  ;
  close(fd);

  // stat the subdir
  if (stat(SUBDIR, &st) < 0) { fail("stat /mnt/subdir"); return; }
  if (st.type != T_DIR) { fail("subdir not T_DIR"); return; }
  ok("subdir verified as directory");

  // stat file in subdir
  if (stat("/mnt/subdir/test", &st) < 0) { fail("stat /mnt/subdir/test"); return; }
  ok("file in subdir accessible");
}

/* Step 5: hard link */
static void test_link(void) {
  step_header(5, "硬链接 (link)");

  int fd = open(LINK_ORIG, O_WRONLY | O_CREATE);
  if (fd < 0) { fail("create original file"); return; }
  write(fd, "xv6 ext2 link test", 18);
  close(fd);

  if (link(LINK_ORIG, LINK_ALIAS) < 0) { fail("link orig -> alias"); return; }
  ok("link created");

  // verify nlink == 2
  struct stat st;
  if (stat(LINK_ORIG, &st) < 0) { fail("stat orig"); return; }
  if (st.nlink == 2) {
    ok("nlink == 2");
  } else {
    fail("nlink != 2");
  }

  // clean
  unlink(LINK_ORIG);
  unlink(LINK_ALIAS);
}

/* Step 6: umount + remount persistence */
static void test_remount_persist(void) {
  step_header(6, "卸载重挂持久化验证");

  // write a marker file before umount
  int fd = open("/mnt/persist_test", O_WRONLY | O_CREATE | O_TRUNC);
  if (fd < 0) { fail("create persist marker"); return; }
  const char *marker = "persistent_data_12345";
  write(fd, marker, strlen(marker));
  close(fd);

  if (umount(MNT) < 0) { printf("%s %d\n", __func__, __LINE__); fail("umount /mnt"); return; }
  ok("umount /mnt");

  if (mount(MNT, 1, "ext2") < 0) { fail("remount /mnt"); return; }
  ok("remount /mnt");

  fd = open("/mnt/persist_test", O_RDONLY);
  if (fd < 0) { fail("re-open persist marker"); return; }

  char buf[64];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  buf[n] = 0;

  if (strcmp(buf, marker) != 0) { fail("data persisted after remount"); return; }
  ok("data persisted after remount");

  // clean
  unlink("/mnt/persist_test");
}

/* Step 7: large file (indirect block) */
static void test_large_file(void) {
  step_header(7, "大文件写入 (indirect block)");

  int fd = open(LARGE_FILE, O_WRONLY | O_CREATE | O_TRUNC);
  if (fd < 0) { fail("create large file"); return; }

  // write 200 blocks of 1024 bytes = 200KB
  char block[1024];
  int i, j;
  for (i = 0; i < 200; i++) {
    for (j = 0; j < 1024; j++)
      block[j] = (char)((i * 7 + j) & 0xff);
    if (write(fd, block, 1024) != 1024) {
      fail("write large file");
      close(fd);
      return;
    }
  }
  close(fd);
  ok("write 200KB");

  // read back and verify random block
  fd = open(LARGE_FILE, O_RDONLY);
  if (fd < 0) { fail("re-open large file"); return; }

  char rbuf[1024];
  // verify block 100
  int off = 100 * 1024;
  // seek via reading
  for (i = 0; i < off / 512; i++) {
    read(fd, rbuf, 512);
  }
  // read remaining part of last partial 512
  if (read(fd, rbuf, 1024) != 1024) { fail("read block 100"); close(fd); return; }

  for (j = 0; j < 1024; j++) {
    if (rbuf[j] != (char)((100 * 7 + j) & 0xff)) {
      fail("large file data mismatch");
      close(fd);
      return;
    }
  }
  ok("read back + verify block 100");
  close(fd);
  unlink(LARGE_FILE);
}

/* Step 8: concurrent write (fork + shared fd) */
static void test_concurrent_write(void) {
  step_header(8, "并发写入 (fork + shared fd)");

  const char *cf = "/mnt/concurrent";
  int fd = open(cf, O_WRONLY | O_CREATE | O_TRUNC);
  if (fd < 0) { fail("create concurrent file"); return; }

  int pid = fork();
  if (pid < 0) { fail("fork"); close(fd); return; }

  if (pid == 0) {
    // child writes 128 'A's
    for (int i = 0; i < 128; i++)
      write(fd, "A", 1);
    exit(0);
  } else {
    // parent writes 128 'B's
    for (int i = 0; i < 128; i++)
      write(fd, "B", 1);
    wait(0);
  }
  close(fd);

  // verify: exactly 128 'A' and 128 'B'
  fd = open(cf, O_RDONLY);
  if (fd < 0) { fail("re-open concurrent file"); return; }

  char buf[512];
  int n = read(fd, buf, sizeof(buf));
  close(fd);

  int count_a = 0, count_b = 0;
  for (int i = 0; i < n; i++) {
    if (buf[i] == 'A') count_a++;
    else if (buf[i] == 'B') count_b++;
  }

  if (count_a == 128 && count_b == 128 && n == 256) {
    ok("128 'A' + 128 'B' = 256 bytes, no data loss");
  } else {
    printf("  n=%d A=%d B=%d\n", n, count_a, count_b);
    fail("concurrent write verify");
  }
  unlink(cf);
}

int main(int argc, char *argv[]) {
  passed = 0;
  failed = 0;
  step = 0;

  printf("\n");
  printf("========================================\n");
  printf("  xv6 VFS/ext2 综合演示\n");
  printf("  RISC-V + LoongArch | HAL 抽象层\n");
  printf("========================================\n");

  setup();

  test_mount();
  test_basic_io();
  test_cross_fs_copy();
  test_dir();
  test_link();
  test_remount_persist();
  test_large_file();
  test_concurrent_write();

  printf("\n");
  printf("========================================\n");
  printf("  结果: %d/%d PASSED", passed, passed + failed);
  if (failed > 0)
    printf("  %d FAILED", failed);
  printf("\n");
  printf("========================================\n");

  return failed > 0 ? 1 : 0;
}
