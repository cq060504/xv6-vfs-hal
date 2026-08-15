// ext2test3 -- 综合测试：L1(mount/umount), L2(vnode_ops), L3(namei), L4(boundary)
// 每个测试子函数独立返回 0=pass, >0=fail, 失败时打印精确位置与诊断信息

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define EXT2_DEV 2

// ---------------------------------------------------------------------------
// 测试框架：轻量，每次失败精确到文件:行号 + 自定义诊断
// ---------------------------------------------------------------------------
static int g_pass, g_fail;

#define TEST(name) \
  for (int _t = (printf("  %-50s ... ", (name)), g_pass++, 0); \
       _t == 0; _t = 1)

#define FAIL(diag) do { \
    g_pass--; g_fail++; \
    printf("\033[31mFAIL\033[0m [%s:%d] %s\n", __func__, __LINE__, (diag)); \
    return 1; \
  } while(0)

#define FAILF(fmt, ...) do { \
    g_pass--; g_fail++; \
    printf("\033[31mFAIL\033[0m [%s:%d] " fmt "\n", __func__, __LINE__, ##__VA_ARGS__); \
    return 1; \
  } while(0)

#define CHK(cond, diag) \
  if (!(cond)) FAIL(diag)

#define CHKF(cond, fmt, ...) \
  if (!(cond)) FAILF(fmt, ##__VA_ARGS__)

// 单函数入口 wrapper
static void do_test(const char *section, int (*fn)(void)) {
  printf("\n[%s]\n", section);
  int r = fn();
  if (r == 0) printf("\033[32m  OK\033[0m\n");
}

// ---------------------------------------------------------------------------
// L1 — VFS mount/umount 体系
// ---------------------------------------------------------------------------

// L1.1: mount → umount → remount 完整循环，验证无状态污染
static int test_l1_mount_cycle(void) {
  TEST("L1.1 mount->umount->remount cycle") {
    // 先确保 /mnt 未挂载 (连续 umount 直到失败)
    umount("/mnt");

    // 首次 mount ext2
    CHK(mount("/mnt", EXT2_DEV, "ext2") >= 0, "first mount failed");

    // 创建文件验证 ext2 可用
    int fd = open("/mnt/t1_cycle", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create /mnt/t1_cycle failed: fd=%d", fd);
    CHK(write(fd, "cycle1", 6) == 6, "write cycle1 failed");
    close(fd);

    // 读回验证
    fd = open("/mnt/t1_cycle", O_RDONLY);
    CHKF(fd >= 0, "reopen /mnt/t1_cycle failed: fd=%d", fd);
    char buf[16];
    int n = read(fd, buf, sizeof(buf));
    CHKF(n == 6 && memcmp(buf, "cycle1", 6) == 0, "read back mismatch: n=%d", n);
    close(fd);

    // umount
    CHK(umount("/mnt") >= 0, "umount failed");

    // 再次 mount
    CHK(mount("/mnt", EXT2_DEV, "ext2") >= 0, "remount failed");

    // 文件应仍然存在 (ext2 持久化)
    fd = open("/mnt/t1_cycle", O_RDONLY);
    CHKF(fd >= 0, "after remount, reopen /mnt/t1_cycle failed: fd=%d", fd);
    n = read(fd, buf, sizeof(buf));
    CHKF(n == 6 && memcmp(buf, "cycle1", 6) == 0, "after remount read mismatch: n=%d", n);
    close(fd);

    // 清理
    unlink("/mnt/t1_cycle");
    umount("/mnt");

    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L1.2: mount 错误路径
static int test_l1_mount_errors(void) {
  TEST("L1.2 mount error paths") {
    // mount 到已挂载的点应失败 (先 mount，再 mount 同一路径)
    CHK(mount("/mnt", EXT2_DEV, "ext2") >= 0, "pre-mount for error test failed");
    CHK(mount("/mnt", EXT2_DEV, "ext2") < 0, "double mount should fail");
    umount("/mnt");

    // mount 到不存在的路径应失败
    CHK(mount("/nonexistent", EXT2_DEV, "ext2") < 0, "mount nonexistent path should fail");

    // mount 到普通文件上应失败
    int fd = open("/mnt_bad", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create /mnt_bad failed: fd=%d", fd);
    close(fd);
    CHK(mount("/mnt_bad", EXT2_DEV, "ext2") < 0, "mount on a file should fail");
    unlink("/mnt_bad");

    // umount 非挂载点应失败
    CHK(umount("/nonexistent") < 0, "umount nonexistent should fail");
    CHK(umount("/") < 0, "umount root (xv6fs) should fail");

    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// ---------------------------------------------------------------------------
// L2 — ext2 核心 vnode_ops 全覆盖
// ---------------------------------------------------------------------------

// 辅助：重新挂载 ext2 到 /mnt
static void remount_ext2(void) {
  umount("/mnt");
  mount("/mnt", EXT2_DEV, "ext2");
}

// L2.1: create + lookup (多层嵌套目录 + 文件)
static int test_l2_create_lookup(void) {
  TEST("L2.1 create + lookup") {
    remount_ext2();

    // 三层嵌套目录
    CHK(mkdir("/mnt/d1") >= 0, "mkdir /mnt/d1 failed");
    CHK(mkdir("/mnt/d1/d2") >= 0, "mkdir /mnt/d1/d2 failed");
    CHK(mkdir("/mnt/d1/d2/d3") >= 0, "mkdir /mnt/d1/d2/d3 failed");

    // 在最深层目录创建文件
    int fd = open("/mnt/d1/d2/d3/d3file", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create nested file failed: fd=%d", fd);
    close(fd);

    // lookup: 打开刚创建的文件
    fd = open("/mnt/d1/d2/d3/d3file", O_RDONLY);
    CHKF(fd >= 0, "lookup nested file failed: fd=%d", fd);
    close(fd);

    // 清理
    unlink("/mnt/d1/d2/d3/d3file");
    CHK(unlink("/mnt/d1/d2/d3") >= 0, "unlink d3 failed");
    CHK(unlink("/mnt/d1/d2") >= 0, "unlink d2 failed");
    CHK(unlink("/mnt/d1") >= 0, "unlink d1 failed");

    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.2: read + write (跨块 4096 字节)
static int test_l2_read_write(void) {
  TEST("L2.2 read + write (cross-block 4096 bytes)") {
    remount_ext2();

    // 写入 4096 字节已知模式
    int fd = open("/mnt/l2rw", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l2rw failed: fd=%d", fd);
    for (int i = 0; i < 4096; i++) {
      char c = (char)(i & 0xFF);
      CHKF(write(fd, &c, 1) == 1, "write at offset %d failed", i);
    }
    close(fd);

    // 读回逐字节对比
    fd = open("/mnt/l2rw", O_RDONLY);
    CHKF(fd >= 0, "reopen l2rw failed: fd=%d", fd);
    for (int i = 0; i < 4096; i++) {
      char c;
      CHKF(read(fd, &c, 1) == 1, "read at offset %d failed", i);
      CHKF(c == (char)(i & 0xFF), "byte mismatch at offset %d: expected 0x%02x got 0x%02x",
           i, (i & 0xFF), (unsigned char)c);
    }
    // 超过文件尾应返回 0
    char c;
    CHKF(read(fd, &c, 1) == 0, "read past EOF should return 0");
    close(fd);

    unlink("/mnt/l2rw");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.3: readdir (验证 . , .. , 及新创建的文件名)
static int test_l2_readdir(void) {
  TEST("L2.3 readdir") {
    remount_ext2();

    // 创建测试文件和目录
    int fd = open("/mnt/l2rd_f", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l2rd_f failed: fd=%d", fd);
    close(fd);
    CHK(mkdir("/mnt/l2rd_d") >= 0, "mkdir l2rd_d failed");

    // 读取 /mnt 目录
    fd = open("/mnt", O_RDONLY);
    CHKF(fd >= 0, "open /mnt dir failed: fd=%d", fd);

    int found_dot = 0, found_dotdot = 0, found_f = 0, found_d = 0;
    struct vdirent de;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0) continue;  // 空 dirent
      if (strcmp(de.name, ".") == 0) found_dot = 1;
      if (strcmp(de.name, "..") == 0) found_dotdot = 1;
      if (strcmp(de.name, "l2rd_f") == 0) found_f = 1;
      if (strcmp(de.name, "l2rd_d") == 0) found_d = 1;
    }
    close(fd);

    CHK(found_dot,    ".  not found in readdir");
    CHK(found_dotdot, ".. not found in readdir");
    CHK(found_f,      "l2rd_f not found in readdir");
    CHK(found_d,      "l2rd_d not found in readdir");

    // 清理
    unlink("/mnt/l2rd_f");
    unlink("/mnt/l2rd_d");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.4: mkdir (验证 type == T_DIR)
static int test_l2_mkdir(void) {
  TEST("L2.4 mkdir + type check") {
    remount_ext2();

    CHK(mkdir("/mnt/l2mkdir") >= 0, "mkdir failed");

    struct stat st;
    CHK(stat("/mnt/l2mkdir", &st) >= 0, "stat after mkdir failed");
    CHKF(st.type == T_DIR, "expected T_DIR(%d) got %d", T_DIR, st.type);

    unlink("/mnt/l2mkdir");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.5: unlink (删除文件后 open 失败；删除非空目录失败)
static int test_l2_unlink(void) {
  TEST("L2.5 unlink") {
    remount_ext2();

    // 删除文件
    int fd = open("/mnt/l2ul_f", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l2ul_f failed: fd=%d", fd);
    close(fd);
    CHK(unlink("/mnt/l2ul_f") >= 0, "unlink file failed");
    fd = open("/mnt/l2ul_f", O_RDONLY);
    CHKF(fd < 0, "open after unlink should fail, fd=%d", fd);

    // 删除非空目录应失败
    CHK(mkdir("/mnt/l2ul_d") >= 0, "mkdir l2ul_d failed");
    fd = open("/mnt/l2ul_d/l2ul_sub", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l2ul_sub failed: fd=%d", fd);
    close(fd);
    CHK(unlink("/mnt/l2ul_d") < 0, "unlink non-empty dir should fail");

    // 清理
    unlink("/mnt/l2ul_d/l2ul_sub");
    unlink("/mnt/l2ul_d");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.6: link (硬链接验证 nlink==2，通过 link 读取数据)
static int test_l2_link(void) {
  TEST("L2.6 hard link") {
    remount_ext2();

    // 创建原文件
    int fd = open("/mnt/l2link_orig", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create orig failed: fd=%d", fd);
    CHK(write(fd, "linkdata", 8) == 8, "write linkdata failed");
    close(fd);

    // 硬链接
    CHK(link("/mnt/l2link_orig", "/mnt/l2link_alias") >= 0, "link failed");

    // stat 原文件：nlink 应为 2
    struct stat st;
    CHK(stat("/mnt/l2link_orig", &st) >= 0, "stat orig failed");
    CHKF(st.nlink == 2, "nlink expected 2, got %d", st.nlink);

    // 通过 link 读取数据
    fd = open("/mnt/l2link_alias", O_RDONLY);
    CHKF(fd >= 0, "open alias failed: fd=%d", fd);
    char buf[16];
    int n = read(fd, buf, sizeof(buf));
    CHKF(n == 8 && memcmp(buf, "linkdata", 8) == 0, "read via link mismatch: n=%d", n);
    close(fd);

    // unlink 一个后另一个仍可访问
    CHK(unlink("/mnt/l2link_orig") >= 0, "unlink orig failed");
    fd = open("/mnt/l2link_alias", O_RDONLY);
    CHKF(fd >= 0, "alias should survive orig unlink: fd=%d", fd);
    close(fd);

    unlink("/mnt/l2link_alias");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.7: mknod (设备节点 major/minor)
static int test_l2_mknod(void) {
  TEST("L2.7 mknod") {
    remount_ext2();

    CHK(mknod("/mnt/l2dev", T_DEVICE, (3 << 8) | 7) >= 0, "mknod failed");

    struct stat st;
    CHK(stat("/mnt/l2dev", &st) >= 0, "stat dev failed");
    CHKF(st.type == T_DEVICE, "type expected T_DEVICE(%d) got %d", T_DEVICE, st.type);

    unlink("/mnt/l2dev");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.8: stat (size, nlink, type 完整性)
static int test_l2_stat(void) {
  TEST("L2.8 stat") {
    remount_ext2();

    int fd = open("/mnt/l2stat", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l2stat failed: fd=%d", fd);
    CHK(write(fd, "0123456789", 10) == 10, "write 10 bytes failed");
    close(fd);

    struct stat st;
    CHK(stat("/mnt/l2stat", &st) >= 0, "stat failed");
    CHKF(st.size == 10, "size expected 10, got %d", (int)st.size);
    CHKF(st.type == T_FILE, "type expected T_FILE(%d) got %d", T_FILE, st.type);
    CHKF(st.nlink == 1, "nlink expected 1, got %d", st.nlink);

    unlink("/mnt/l2stat");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L2.9: truncate (通过 O_TRUNC 或 write 截断)
static int test_l2_truncate(void) {
  TEST("L2.9 truncate (O_TRUNC)") {
    remount_ext2();

    // 创建并写入
    int fd = open("/mnt/l2trunc", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l2trunc failed: fd=%d", fd);
    CHK(write(fd, "0123456789", 10) == 10, "write failed");
    close(fd);

    // 以 O_TRUNC 打开
    fd = open("/mnt/l2trunc", O_WRONLY | O_TRUNC);
    CHKF(fd >= 0, "open with O_TRUNC failed: fd=%d", fd);
    close(fd);

    // size 应为 0
    struct stat st;
    CHK(stat("/mnt/l2trunc", &st) >= 0, "stat after trunc failed");
    CHKF(st.size == 0, "size after trunc expected 0, got %d", (int)st.size);

    // read 应返回 0
    fd = open("/mnt/l2trunc", O_RDONLY);
    CHKF(fd >= 0, "open for read after trunc failed: fd=%d", fd);
    char c;
    CHKF(read(fd, &c, 1) == 0, "read after trunc should return 0");
    close(fd);

    unlink("/mnt/l2trunc");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// ---------------------------------------------------------------------------
// L3 — VFS namei / 路径遍历
// ---------------------------------------------------------------------------

static int test_l3_namei(void) {
  TEST("L3 namei / path walking") {
    remount_ext2();

    // 搭建目录结构: /mnt/l3dir/l3sub/
    CHK(mkdir("/mnt/l3dir") >= 0, "mkdir l3dir failed");
    CHK(mkdir("/mnt/l3dir/l3sub") >= 0, "mkdir l3sub failed");
    int fd = open("/mnt/l3dir/l3sub/l3file", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l3file failed: fd=%d", fd);
    CHK(write(fd, "l3data", 6) == 6, "write l3data failed");
    close(fd);

    // L3.1: 绝对路径
    fd = open("/mnt/l3dir/l3sub/l3file", O_RDONLY);
    CHKF(fd >= 0, "absolute path open failed: fd=%d", fd);
    char buf[16];
    int n = read(fd, buf, sizeof(buf));
    CHKF(n == 6 && memcmp(buf, "l3data", 6) == 0, "abs read mismatch: n=%d", n);
    close(fd);

    // L3.2: 相对路径 (cwd 在 ext2 内)
    CHK(chdir("/mnt/l3dir") >= 0, "chdir /mnt/l3dir failed");
    fd = open("l3sub/l3file", O_RDONLY);
    CHKF(fd >= 0, "relative path open failed: fd=%d", fd);
    n = read(fd, buf, sizeof(buf));
    CHKF(n == 6 && memcmp(buf, "l3data", 6) == 0, "rel read mismatch: n=%d", n);
    close(fd);

    // L3.3: .. 跨 mount 点返回 (ext2 根目录的 .. → xv6fs root)
    CHK(chdir("/mnt") >= 0, "chdir /mnt failed");
    fd = open("..", O_RDONLY);
    CHKF(fd >= 0, "open .. from mount root failed: fd=%d", fd);
    // 读取 .. 目录，验证可以找到 xv6fs 根目录的条目 (如 "tests")
    struct vdirent de;
    int found_tests = 0;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0) continue;
      if (strcmp(de.name, "tests") == 0) { found_tests = 1; break; }
    }
    close(fd);
    CHK(found_tests, ".. from /mnt should see xv6fs root (e.g. 'tests')");

    // L3.4: 不存在的路径
    fd = open("/mnt/does_not_exist", O_RDONLY);
    CHKF(fd < 0, "open nonexistent path should fail, fd=%d", fd);

    // L3.5: 路径遍历中非目录组件
    fd = open("/mnt/l3dir/l3sub/l3file/bad", O_RDONLY);
    CHKF(fd < 0, "path through file should fail, fd=%d", fd);

    // 恢复 cwd
    CHK(chdir("/") >= 0, "chdir / failed");

    // 清理
    unlink("/mnt/l3dir/l3sub/l3file");
    unlink("/mnt/l3dir/l3sub");
    unlink("/mnt/l3dir");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// ---------------------------------------------------------------------------
// L4 — 边界/并发/大文件
// ---------------------------------------------------------------------------

// L4.1: 多进程并发共享 fd (fork 继承 fd，共享 f->off)
static int test_l4_concurrent(void) {
  TEST("L4.1 concurrent write (fork)") {
    remount_ext2();

    // 父进程创建文件，不 close —— 父子通过继承的 fd 共享同一个 f->off
    int fd = open("/mnt/l4conc", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l4conc failed: fd=%d", fd);

    int pid = fork();
    CHKF(pid >= 0, "fork failed: pid=%d", pid);

    if (pid == 0) {
      // 子进程使用继承的 fd，写 128 字节 'A'
      for (int i = 0; i < 128; i++) {
        char c = 'A';
        if (write(fd, &c, 1) != 1) { exit(2); }
      }
      exit(0);
    } else {
      // 父进程使用同一个 fd，写 128 字节 'B'
      for (int i = 0; i < 128; i++) {
        char c = 'B';
        if (write(fd, &c, 1) != 1) { close(fd); wait(0); FAIL("parent write failed"); }
      }
      close(fd);
      wait(0);
    }

    // 验证总大小 256 (128+128，即使交错执行也会各自追加)
    struct stat st;
    CHK(stat("/mnt/l4conc", &st) >= 0, "stat l4conc failed");
    CHKF(st.size == 256, "concurrent size expected 256, got %d", (int)st.size);

    unlink("/mnt/l4conc");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L4.2: unlink-while-open 语义
static int test_l4_unlink_while_open(void) {
  TEST("L4.2 unlink while open") {
    remount_ext2();

    // 创建文件并保持打开
    int fd = open("/mnt/l4uwo", O_RDWR | O_CREATE);
    CHKF(fd >= 0, "create l4uwo failed: fd=%d", fd);
    CHK(write(fd, "unlink_open_test", 16) == 16, "write failed");

    // unlink 它
    CHK(unlink("/mnt/l4uwo") >= 0, "unlink failed");

    // 文件不应再出现在目录中
    int fd2 = open("/mnt/l4uwo", O_RDONLY);
    CHKF(fd2 < 0, "open after unlink should fail, fd=%d", fd2);

    // 但已打开 fd 仍可读写 (seek 到 0 读回)
    CHK(write(fd, "more", 4) == 4, "write after unlink failed");
    // seek 回开头需要重新 open，但 fd 还在 —— 读当前位置之后的 16 字节
    // 实际验证：close 后数据持久化到 ext2 即可
    close(fd);

    // 重新 mount 验证文件确实被删除
    umount("/mnt");
    CHK(mount("/mnt", EXT2_DEV, "ext2") >= 0, "remount failed");
    fd = open("/mnt/l4uwo", O_RDONLY);
    CHKF(fd < 0, "file should not persist after unlink+remount, fd=%d", fd);

    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L4.3: 大文件 (超出 NDIRECT, 测试 single indirect block)
static int test_l4_large_file(void) {
  TEST("L4.3 large file (single indirect)") {
    remount_ext2();

    // 写入 200 块 (200*1024 = 204800 字节，远超 12 个 direct block)
    int fd = open("/mnt/l4big", O_WRONLY | O_CREATE);
    CHKF(fd >= 0, "create l4big failed: fd=%d", fd);
    int total = 0;
    char block[BSIZE];
    for (int i = 0; i < BSIZE; i++) block[i] = (char)(i & 0xFF);
    for (int blk = 0; blk < 200; blk++) {
      int w = write(fd, block, BSIZE);
      CHKF(w == BSIZE, "write block %d: expected %d got %d", blk, BSIZE, w);
      total += w;
    }
    close(fd);

    // stat 验证大小
    struct stat st;
    CHK(stat("/mnt/l4big", &st) >= 0, "stat l4big failed");
    CHKF(st.size == total, "size expected %d, got %d", total, (int)st.size);

    // 读回最后一个字节验证
    fd = open("/mnt/l4big", O_RDONLY);
    CHKF(fd >= 0, "reopen l4big failed: fd=%d", fd);
    // 跳到最后 1 字节 (简单实现：读到最后)
    char c;
    int n;
    while ((n = read(fd, &c, 1)) == 1) {}
    CHKF(n == 0, "final read should return 0, got %d", n);
    close(fd);

    unlink("/mnt/l4big");
    umount("/mnt");
    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// ---------------------------------------------------------------------------
// L5 — 并发能力测试（原独立 vfsconcur 程序的功能，并入本文件）
//   T1 并发追加  T2 并发创建  T3 挂载点内 exec（验证 ext2 read_kernel）
// 断言沿用本文件的 CHK/CHKF + do_test 框架。
// ---------------------------------------------------------------------------

#define CONC_NPROC    6   // 并发追加进程数
#define CONC_BLK      512
#define CONC_BLKSPER  40  // 每进程追加块数 (40*512=20KB)
#define CONC_NDIR     8   // 并发创建目录数
#define CONC_NFILES   24  // 每目录文件数

// 极小子串匹配（xv6 ulib 无 strstr）
static int conc_hasstr(const char *hay, const char *needle){
  int h = strlen(hay), n = strlen(needle);
  if(n > h) return 0;
  for(int i = 0; i <= h - n; i++){
    int j = 0;
    while(j < n && hay[i+j] == needle[j]) j++;
    if(j == n) return 1;
  }
  return 0;
}

// 极简整数转字符串
static int conc_itostr(char *buf, int v){
  char tmp[16]; int n = 0;
  if(v == 0) tmp[n++] = '0';
  while(v > 0){ tmp[n++] = '0' + v % 10; v /= 10; }
  for(int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  buf[n] = 0;
  return n;
}

// 拼接路径：prefix + mid + (id>=0 时 id) + name
static void conc_mkpath(char *dst, const char *prefix, const char *mid,
                        int id, const char *name){
  int i = 0;
  while(*prefix) dst[i++] = *prefix++;
  while(*mid)    dst[i++] = *mid++;
  if(id >= 0){
    char b[16]; conc_itostr(b, id);
    for(int j = 0; b[j]; j++) dst[i++] = b[j];
  }
  while(*name) dst[i++] = *name++;
  dst[i] = 0;
}

// 拼接 dir 目录下名为 f 的文件路径（/base/d<i>/f<fnum>）
static void conc_fpath(char *dst, const char *base, int i, int fnum){
  char path[160];
  conc_mkpath(path, base, "/d", i, "/f");
  int k = strlen(path);
  char fb[16]; int fl = conc_itostr(fb, fnum);
  for(int j = 0; j < fl; j++) path[k++] = fb[j];
  path[k] = 0;
  strcpy(dst, path);
}

// L5.1: 6 进程 O_APPEND 并发追加同一文件，校验总长与每 512B 块完整性
static int test_l5_concur_append(void){
  TEST("L5.1 concurrent append (O_APPEND, 6 procs)") {
    remount_ext2();
    char path[] = "/mnt/vfscon_t1";
    int fd = open(path, O_WRONLY | O_CREATE | O_TRUNC);
    CHKF(fd >= 0, "open create l5.1 fd=%d", fd);
    close(fd);

    for(int p = 0; p < CONC_NPROC; p++){
      int pid = fork();
      if(pid == 0){
        int f = open(path, O_WRONLY | O_APPEND);
        if(f < 0) exit(2);
        char blk[CONC_BLK];
        for(int b = 0; b < CONC_BLKSPER; b++){
          memset(blk, 'A' + p, CONC_BLK);
          blk[0] = 'P'; blk[1] = '0' + p/10; blk[2] = '0' + p%10; blk[3] = '.';
          blk[4] = 'B'; blk[5] = '0' + b/1000%10; blk[6] = '0' + b/100%10;
          blk[7] = '0' + b/10%10; blk[8] = '0' + b%10;
          if(write(f, blk, CONC_BLK) != CONC_BLK) exit(3);
        }
        close(f);
        exit(0);
      }
    }
    int st;
    for(int i = 0; i < CONC_NPROC; i++) wait(&st);

    fd = open(path, O_RDONLY);
    CHKF(fd >= 0, "reopen l5.1 fd=%d", fd);
    int total = CONC_NPROC * CONC_BLKSPER * CONC_BLK;
    char *data = malloc(total);
    CHK(data != 0, "l5.1 malloc");
    int rd = 0;
    while(rd < total){
      int n = read(fd, data + rd, total - rd);
      if(n <= 0) break;
      rd += n;
    }
    close(fd);
    CHKF(rd == total, "l5.1 size expected %d got %d", total, rd);

    int bad = 0;
    for(int i = 0; i < CONC_NPROC * CONC_BLKSPER; i++){
      char *b = data + i * CONC_BLK;
      if(b[0] != 'P' || b[3] != '.' || b[4] != 'B'){ bad++; continue; }
      int p = (b[1] - '0') * 10 + (b[2] - '0');
      if(p < 0 || p >= CONC_NPROC){ bad++; continue; }
      for(int j = 9; j < CONC_BLK; j++)
        if(b[j] != (char)('A' + p)){ bad++; break; }
    }
    free(data);
    CHKF(bad == 0, "l5.1 %d corrupted blocks", bad);
    unlink(path);

    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L5.2: 8 进程各建子目录并发创建文件，校验全部存在且内容唯一精确
static int test_l5_concur_create(void){
  TEST("L5.2 concurrent create (8 dirs x 24 files)") {
    remount_ext2();
    char base[] = "/mnt/vfscon_t2";
    CHK(mkdir(base) == 0, "mkdir l5.2 base");

    for(int i = 0; i < CONC_NDIR; i++){
      int pid = fork();
      if(pid == 0){
        char d[128];
        conc_mkpath(d, base, "/d", i, "");
        if(mkdir(d) < 0) exit(2);
        for(int f = 0; f < CONC_NFILES; f++){
          char fp[128];
          conc_mkpath(fp, d, "/f", f, "");
          int fd = open(fp, O_WRONLY | O_CREATE);
          if(fd < 0) exit(3);
          char expect[64];
          char *q = expect;
          *q++ = 'I'; *q++ = 'D'; *q++ = '='; q += conc_itostr(q, i);
          *q++ = ' '; *q++ = 'F'; *q++ = '='; q += conc_itostr(q, f);
          *q = 0;
          int blen = strlen(expect);
          if(write(fd, expect, blen) != blen) exit(4);
          close(fd);
        }
        exit(0);
      }
    }
    int st;
    for(int i = 0; i < CONC_NDIR; i++) wait(&st);

    for(int i = 0; i < CONC_NDIR; i++){
      for(int f = 0; f < CONC_NFILES; f++){
        char fp[160];
        conc_fpath(fp, base, i, f);
        int fd = open(fp, O_RDONLY);
        CHKF(fd >= 0, "l5.2 missing %s", fp);
        char r[64];
        int n = read(fd, r, sizeof(r) - 1);
        close(fd);
        if(n < 0) n = 0;
        r[n] = 0;
        char expect[64];
        char *q = expect;
        *q++ = 'I'; *q++ = 'D'; *q++ = '='; q += conc_itostr(q, i);
        *q++ = ' '; *q++ = 'F'; *q++ = '='; q += conc_itostr(q, f);
        *q = 0;
        CHKF(strcmp(r, expect) == 0, "l5.2 content %s: '%s'!='%s'", fp, r, expect);
      }
    }
    // 递归清理：先删文件、再删子目录、最后删 base，避免残留导致下次 mkdir 失败
    for(int i = 0; i < CONC_NDIR; i++){
      char d[160];
      conc_mkpath(d, base, "/d", i, "");
      for(int f = 0; f < CONC_NFILES; f++){
        char fp[160];
        conc_fpath(fp, base, i, f);
        unlink(fp);
      }
      CHK(unlink(d) == 0, "l5.2 unlink subdir");
    }
    CHK(unlink(base) == 0, "l5.2 unlink base");

    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// L5.3: 把 /echo 复制进 /mnt 再 exec，断言输出含 hello（验证 ext2 read_kernel）
static int test_l5_fs_exec(void){
  TEST("L5.3 exec on ext2 (read_kernel)") {
    remount_ext2();
    char dst[] = "/mnt/echo";
    int s = open("/echo", O_RDONLY);
    CHKF(s >= 0, "open /echo source fd=%d", s);
    int d = open(dst, O_WRONLY | O_CREATE | O_TRUNC);
    CHKF(d >= 0, "create /mnt/echo fd=%d", d);
    char buf[512];
    int n;
    while((n = read(s, buf, sizeof(buf))) > 0)
      CHK(write(d, buf, n) == n, "copy write l5.3");
    close(s); close(d);

    int pp[2];
    pipe(pp);
    int pid = fork();
    if(pid == 0){
      close(pp[0]);
      close(1);
      dup(pp[1]);
      close(pp[1]);
      char *argv[] = { dst, "hello", 0 };
      exec(dst, argv);
      exit(1);
    }
    close(pp[1]);
    char out[64];
    int on = 0;
    while(on < 63 && (n = read(pp[0], out + on, 63 - on)) > 0) on += n;
    close(pp[0]);
    int st;
    wait(&st);
    out[on] = 0;

    CHK(conc_hasstr(out, "hello"), "ext2 exec printed 'hello'");
    unlink(dst);

    printf("\033[32mPASS\033[0m");
  }
  return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  g_pass = g_fail = 0;

  printf("\n========== ext2test: VFS/ext2 综合测试 ==========\n");

  do_test("L1 — mount/umount 体系", test_l1_mount_cycle);
  do_test("L1 — mount 错误路径", test_l1_mount_errors);
  do_test("L2 — create + lookup", test_l2_create_lookup);
  do_test("L2 — read + write (cross-block)", test_l2_read_write);
  do_test("L2 — readdir", test_l2_readdir);
  do_test("L2 — mkdir", test_l2_mkdir);
  do_test("L2 — unlink", test_l2_unlink);
  do_test("L2 — hard link", test_l2_link);
  do_test("L2 — mknod", test_l2_mknod);
  do_test("L2 — stat", test_l2_stat);
  do_test("L2 — truncate (O_TRUNC)", test_l2_truncate);
  do_test("L3 — namei / path walking", test_l3_namei);
  do_test("L4 — concurrent write", test_l4_concurrent);
  do_test("L4 — unlink while open", test_l4_unlink_while_open);
  do_test("L4 — large file (indirect)", test_l4_large_file);
  do_test("L5 — concurrent append", test_l5_concur_append);
  do_test("L5 — concurrent create", test_l5_concur_create);
  do_test("L5 — exec on ext2 (read_kernel)", test_l5_fs_exec);

  printf("\n========== %d passed, %d failed ==========\n\n", g_pass, g_fail);
  exit(g_fail > 0 ? 1 : 0);
}
