// fat32test: comprehensive FAT32 verification for the xv6 VFS.
//
// Covers:
//   mount/umount
//   create + write + read + stat (incl. size persistence across reopen, B1)
//   O_APPEND  /  O_TRUNC
//   multi-cluster file (crosses cluster boundaries, exercises alloc_clus)
//   mkdir + nested file + sub-directory readdir (".", "..", entries)
//   "." / ".." path lookup (B4)
//   root readdir listing
//   LFN (<= 13 chars) create/read round-trip + readdir verbatim display
//   LFN (> 13 chars): readdir shows the FULL long name (VDIRSIZ=256) and it
//   can be reopened by that full name
//   unlink (plain, LFN, non-empty-directory rejection, empty directory)

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"
#include "kernel/fcntl.h"

static int failed = 0;

static void
check(int ok, char *msg)
{
  if(ok)
    printf("PASS: %s\n", msg);
  else {
    printf("FAIL: %s\n", msg);
    failed = 1;
  }
}

// ---------------------------------------------------------------------------
// 并发能力测试（原独立 vfsconcur 程序的功能，并入本文件）：
//   T1 并发追加  T2 并发创建  T3 挂载点内 exec
// 断言沿用本文件的 check()/failed。
// ---------------------------------------------------------------------------

#define CONC_NPROC    6   // 并发追加进程数
#define CONC_BLK      512
#define CONC_BLKSPER  40  // 每进程追加块数 (40*512=20KB)
#define CONC_NDIR     8   // 并发创建目录数
#define CONC_NFILES   24  // 每目录文件数

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

static int conc_itostr(char *buf, int v){
  char tmp[16]; int n = 0;
  if(v == 0) tmp[n++] = '0';
  while(v > 0){ tmp[n++] = '0' + v % 10; v /= 10; }
  for(int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  buf[n] = 0;
  return n;
}

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

// T1: 6 进程 O_APPEND 并发追加同一文件，校验总长与每 512B 块完整性
static void
vfsconcur_t1_append(const char *prefix)
{
  printf("-- T1: concurrent append (%d procs x %d x %d B)\n",
         CONC_NPROC, CONC_BLKSPER, CONC_BLK);
  char path[128];
  conc_mkpath(path, prefix, "/vfscon_t1", -1, "");

  int fd = open(path, O_WRONLY | O_CREATE | O_TRUNC);
  check(fd >= 0, "open create t1");
  if(fd < 0) return;
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
  check(fd >= 0, "reopen t1");
  if(fd < 0) return;
  int total = CONC_NPROC * CONC_BLKSPER * CONC_BLK;
  char *data = malloc(total);
  if(data == 0){ close(fd); check(0, "t1 malloc"); return; }
  int rd = 0;
  while(rd < total){
    int n = read(fd, data + rd, total - rd);
    if(n <= 0) break;
    rd += n;
  }
  close(fd);
  if(rd != total){
    printf("FAIL: size mismatch expected %d got %d\n", total, rd);
    failed = 1;
  }
  int bad = 0;
  for(int i = 0; i < CONC_NPROC * CONC_BLKSPER; i++){
    char *b = data + i * CONC_BLK;
    if(b[0] != 'P' || b[3] != '.' || b[4] != 'B'){ bad++; continue; }
    int p = (b[1] - '0') * 10 + (b[2] - '0');
    if(p < 0 || p >= CONC_NPROC){ bad++; continue; }
    for(int j = 9; j < CONC_BLK; j++)
      if(b[j] != (char)('A' + p)){ bad++; break; }
  }
  check(bad == 0, "all blocks intact (no torn/overwritten)");
  if(bad) printf("    %d bad blocks\n", bad);
  free(data);
  unlink(path);
}

// T2: 8 进程各建子目录并发创建文件，校验全部存在且内容唯一精确
static void
conc_build_expect(char *out, int d, int f)
{
  char *q = out;
  *q++ = 'I'; *q++ = 'D'; *q++ = '='; q += conc_itostr(q, d);
  *q++ = ' '; *q++ = 'F'; *q++ = '='; q += conc_itostr(q, f);
  *q = 0;
}

static void
vfsconcur_t2_create(const char *prefix)
{
  printf("-- T2: concurrent create (%d dirs x %d files)\n",
         CONC_NDIR, CONC_NFILES);
  char p[128];
  conc_mkpath(p, prefix, "/vfscon_t2", -1, "");
  check(mkdir(p) == 0, "mkdir base t2");

  for(int i = 0; i < CONC_NDIR; i++){
    int pid = fork();
    if(pid == 0){
      char d[128];
      conc_mkpath(d, p, "/d", i, "");
      if(mkdir(d) < 0) exit(2);
      for(int f = 0; f < CONC_NFILES; f++){
        char fp[128];
        conc_mkpath(fp, d, "/f", f, "");
        int fd = open(fp, O_WRONLY | O_CREATE);
        if(fd < 0) exit(3);
        char expect[40];
        conc_build_expect(expect, i, f);
        int blen = strlen(expect);
        if(write(fd, expect, blen) != blen) exit(4);
        close(fd);
      }
      exit(0);
    }
  }
  int st;
  for(int i = 0; i < CONC_NDIR; i++) wait(&st);

  int ok = 1;
  for(int i = 0; i < CONC_NDIR; i++){
    char d[128];
    conc_mkpath(d, p, "/d", i, "");
    for(int f = 0; f < CONC_NFILES; f++){
      char fp[128];
      conc_mkpath(fp, d, "/f", f, "");
      int fd = open(fp, O_RDONLY);
      if(fd < 0){
        printf("FAIL: missing %s\n", fp);
        ok = 0;
        continue;
      }
      char r[40];
      int n = read(fd, r, sizeof(r) - 1);
      close(fd);
      if(n < 0) n = 0;
      r[n] = 0;
      char expect[40];
      conc_build_expect(expect, i, f);
      if(strcmp(r, expect) != 0){
        printf("FAIL: content mismatch %s: '%s' != '%s'\n", fp, r, expect);
        ok = 0;
      }
    }
  }
  check(ok, "all files present with exact unique content");
  if(!ok) failed = 1;
  unlink(p);
}

// T3: 把 /echo 复制进挂载点再 exec，断言输出含 hello（验证 read_kernel）
static void
vfsconcur_t3_exec(const char *prefix)
{
  printf("-- T3: copy /echo -> %s/echo and exec\n", prefix);
  char dst[128];
  conc_mkpath(dst, prefix, "/echo", -1, "");

  int s = open("/echo", O_RDONLY);
  check(s >= 0, "open /echo source");
  if(s < 0) return;
  int d = open(dst, O_WRONLY | O_CREATE | O_TRUNC);
  check(d >= 0, "create dst echo");
  if(d < 0){ close(s); return; }
  char buf[512];
  int n;
  while((n = read(s, buf, sizeof(buf))) > 0)
    if(write(d, buf, n) != n){ check(0, "copy write"); break; }
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

  check(conc_hasstr(out, "hello"), "FAT32 exec printed 'hello'");
  if(!conc_hasstr(out, "hello")) printf("    output='%s'\n", out);
  unlink(dst);
}

// ---- directory listing helper --------------------------------------
struct dlist {
  char name[VDIRSIZ];
  ushort inum;
};

// Read all directory entries of `path` into `out` (max `max` entries).
// Returns entry count, or -1 on open failure.
static int
readdirall(char *path, struct dlist *out, int max)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  struct vdirent de;
  int cnt = 0;
  while(cnt < max && read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0) continue;
    strcpy(out[cnt].name, de.name);
    out[cnt].inum = de.inum;
    cnt++;
  }
  close(fd);
  return cnt;
}

static int
hasname(struct dlist *l, int n, char *want)
{
  for(int i = 0; i < n; i++)
    if(strcmp(l[i].name, want) == 0) return i;
  return -1;
}

// ---- read/write round-trip helper -----------------------------------
// Writes n bytes of 'A'+i%26 pattern, closes, reopens, verifies content
// and file size.
static void
rwcheck(char *path, int n)
{
  char *buf = malloc(n ? n : 1);
  char *rb  = malloc(n ? n : 1);
  int i;
  for(i = 0; i < n; i++) buf[i] = 'A' + (i % 26);

  int fd = open(path, O_WRONLY | O_CREATE | O_TRUNC);
  if(fd < 0){
    printf("FAIL: rwcheck open write %s\n", path);
    failed = 1; free(buf); free(rb); return;
  }
  int off = 0;
  while(off < n){
    int w = write(fd, buf + off, n - off);
    if(w <= 0){
      printf("FAIL: rwcheck write %s off=%d\n", path, off);
      close(fd); failed = 1; free(buf); free(rb); return;
    }
    off += w;
  }
  close(fd);

  fd = open(path, O_RDONLY);
  if(fd < 0){
    printf("FAIL: rwcheck open read %s\n", path);
    failed = 1; free(buf); free(rb); return;
  }
  int got = 0, r;
  while(got < n && (r = read(fd, rb + got, n - got)) > 0)
    got += r;
  close(fd);

  if(got != n || memcmp(buf, rb, n) != 0){
    printf("FAIL: rwcheck content %s (got=%d want=%d)\n", path, got, n);
    failed = 1;
  } else {
    printf("PASS: rwcheck content %s (%d bytes)\n", path, n);
  }

  // size must survive close + reopen (B1 sync_dirent)
  struct stat st;
  fd = open(path, O_RDONLY);
  if(fd >= 0){
    if(fstat(fd, &st) == 0){
      if(st.size == n)
        printf("PASS: rwcheck size %s (%d)\n", path, n);
      else {
        printf("FAIL: rwcheck size %s got=%ld want=%d\n", path, (long)st.size, n);
        failed = 1;
      }
    }
    close(fd);
  }

  free(buf); free(rb);
}

int
main(void)
{
  struct stat st;
  int fd;

  // oldl/newl hold up to 48 directory entries; each entry is 258 bytes
  // (VDIRSIZ=256 + inum), so 48 entries = ~12KB.  xv6's initial user
  // stack is only one 4KB page, so these MUST live on the heap, not
  // on the stack (a stack allocation overflows into the guard page and
  // traps with scause 0xf / "unexpected scause 0xf").
  struct dlist *oldl = malloc(sizeof(struct dlist) * 48);
  struct dlist *newl = malloc(sizeof(struct dlist) * 48);
  if(oldl == 0 || newl == 0){
    printf("fat32test: out of memory\n");
    exit(1);
  }

  printf("fat32test: starting\n");

  // ---- 1. mount ---- (/fat created by init.c)
  check(mount("/fat", 3, "fat32") == 0, "mount /fat 3 fat32");

  // ---- 2. basic create/write/read/stat ----
  rwcheck("/fat/hello.txt", 16);
  fd = open("/fat/hello.txt", O_RDONLY);
  if(fd < 0){ printf("FAIL: reopen hello.txt\n"); failed = 1; }
  else {
    if(fstat(fd, &st) == 0 && st.type == T_FILE)
      printf("PASS: hello.txt type T_FILE\n");
    else { printf("FAIL: hello.txt type\n"); failed = 1; }
    close(fd);
  }

  // ---- 3. O_APPEND ----
  fd = open("/fat/append.txt", O_WRONLY | O_CREATE | O_TRUNC);
  check(fd >= 0, "open append.txt (create)");
  if(fd >= 0){ write(fd, "abc", 3); close(fd); }
  fd = open("/fat/append.txt", O_WRONLY | O_APPEND);
  check(fd >= 0, "open append.txt (append)");
  if(fd >= 0){ write(fd, "def", 3); close(fd); }
  fd = open("/fat/append.txt", O_RDONLY);
  if(fd < 0){ printf("FAIL: open append.txt read\n"); failed = 1; }
  else {
    char rbuf[16];
    int n = read(fd, rbuf, 16);
    close(fd);
    check(n == 6 && memcmp(rbuf, "abcdef", 6) == 0, "O_APPEND content");
  }

  // ---- 4. O_TRUNC ----
  fd = open("/fat/trunc.txt", O_WRONLY | O_CREATE);
  if(fd >= 0){ write(fd, "0123456789", 10); close(fd); }
  fd = open("/fat/trunc.txt", O_WRONLY | O_TRUNC);
  check(fd >= 0, "open trunc.txt (O_TRUNC)");
  if(fd >= 0) close(fd);
  fd = open("/fat/trunc.txt", O_RDONLY);
  if(fd < 0){ printf("FAIL: open trunc.txt read\n"); failed = 1; }
  else {
    if(fstat(fd, &st) == 0)
      check(st.size == 0, "O_TRUNC size zero");
    close(fd);
  }
  // Rewrite without O_TRUNC.  NOTE: FAT32 stores the file size in the
  // directory entry; ftrunc() persists size=0 back to disk (B1), so a plain
  // O_WRONLY open starts at offset 0 and yields size == bytes written.
  // This is genuine FAT32 semantics, NOT the POSIX "size preserved on
  // overwrite" behaviour this test originally assumed.
  fd = open("/fat/trunc.txt", O_WRONLY);
  if(fd >= 0){ write(fd, "NEW", 3); close(fd); }
  fd = open("/fat/trunc.txt", O_RDONLY);
  if(fd < 0){ printf("FAIL: open trunc.txt rewrite read\n"); failed = 1; }
  else {
    char rbuf[16];
    int n = read(fd, rbuf, 16);
    close(fd);
    check(n == 3 && memcmp(rbuf, "NEW", 3) == 0,
          "write after truncate yields size == written bytes (FAT32)");
  }

  // ---- 5. multi-cluster file (10 KB crosses many clusters) ----
  rwcheck("/fat/multi.bin", 10000);

  // ---- 6. mkdir + nested file ----
  check(mkdir("/fat/subdir") == 0, "mkdir /fat/subdir");
  rwcheck("/fat/subdir/nested.txt", 6);

  // ---- 7. sub-directory readdir: ".", "..", "nested.txt" ----
  int scnt = readdirall("/fat/subdir", oldl, 32);
  check(scnt >= 3, "subdir readdir count");
  check(hasname(oldl, scnt, ".") >= 0, "subdir contains .");
  check(hasname(oldl, scnt, "..") >= 0, "subdir contains ..");
  check(hasname(oldl, scnt, "nested.txt") >= 0, "subdir contains nested.txt");

  // ---- 8. "." / ".." path lookup (B4) ----
  fd = open("/fat/subdir/.", O_RDONLY);
  check(fd >= 0, "lookup /fat/subdir/.");
  if(fd >= 0) close(fd);
  fd = open("/fat/subdir/..", O_RDONLY);
  check(fd >= 0, "lookup /fat/subdir/..");
  if(fd >= 0) close(fd);
  fd = open("/fat/..", O_RDONLY);
  check(fd >= 0, "lookup /fat/..");
  if(fd >= 0) close(fd);

  // ---- 9. root readdir lists expected entries ----
  int rcnt = readdirall("/fat", oldl, 48);
  check(rcnt >= 5, "root readdir count");
  check(hasname(oldl, rcnt, "hello.txt") >= 0, "root has hello.txt");
  check(hasname(oldl, rcnt, "append.txt") >= 0, "root has append.txt");
  check(hasname(oldl, rcnt, "trunc.txt") >= 0, "root has trunc.txt");
  check(hasname(oldl, rcnt, "multi.bin") >= 0, "root has multi.bin");
  check(hasname(oldl, rcnt, "subdir") >= 0, "root has subdir");

  // ---- 10. LFN <= 13 chars: full round-trip + readdir verbatim ----
  rwcheck("/fat/FileDataX.txt", 20);
  rcnt = readdirall("/fat", oldl, 48);
  // FAT LFN entries preserve the original case exactly as created;
  // readdir therefore reports "FileDataX.txt" (not lowercased).
  check(hasname(oldl, rcnt, "FileDataX.txt") >= 0,
        "readdir shows LFN <=13 verbatim (case preserved)");

  // ---- 11. LFN > 13 chars: full name visible + reopen by that name ----
  // With VDIRSIZ=256 the long name is now reported in full, so we verify
  // the directory listing shows "LongFileNameTest.txt" (not a truncated
  // short-name fallback) and that it can be reopened by its full name.
  char *ldata = "long-data-content-123456";
  fd = open("/fat/LongFileNameTest.txt", O_WRONLY | O_CREATE | O_TRUNC);
  check(fd >= 0, "create LFN >13");
  if(fd >= 0){ write(fd, ldata, 23); close(fd); }

  int nc = readdirall("/fat", newl, 48);
  check(hasname(newl, nc, "LongFileNameTest.txt") >= 0,
        "readdir shows full LFN >13 name");
  fd = open("/fat/LongFileNameTest.txt", O_RDONLY);
  if(fd < 0){
    printf("FAIL: reopen LFN >13 by full name\n");
    failed = 1;
  } else {
    char rbuf[64];
    int rn = read(fd, rbuf, 64);
    close(fd);
    check(rn == 23 && memcmp(rbuf, ldata, 23) == 0,
          "reopen LFN >13 by full name");
  }

  // ---- 12. unlink ----
  check(unlink("/fat/hello.txt") == 0, "unlink hello.txt");
  fd = open("/fat/hello.txt", O_RDONLY);
  check(fd < 0, "unlinked hello.txt gone");
  if(fd >= 0) close(fd);

  check(unlink("/fat/LongFileNameTest.txt") == 0, "unlink LFN file");
  fd = open("/fat/LongFileNameTest.txt", O_RDONLY);
  check(fd < 0, "unlinked LFN file gone");
  if(fd >= 0) close(fd);

  // non-empty directory must be rejected
  check(unlink("/fat/subdir") < 0, "unlink non-empty dir rejected");

  // now empty the subdir and remove it
  check(unlink("/fat/subdir/nested.txt") == 0, "unlink nested.txt");
  check(unlink("/fat/subdir") == 0, "unlink empty subdir");

  // ---- 13. cleanup remaining + umount ----
  unlink("/fat/append.txt");
  unlink("/fat/trunc.txt");
  unlink("/fat/multi.bin");
  unlink("/fat/FileDataX.txt");

  // ---- 14. 并发能力测试（原 vfsconcur 并入）----
  vfsconcur_t1_append("/fat");
  vfsconcur_t2_create("/fat");
  vfsconcur_t3_exec("/fat");

  check(umount("/fat") == 0, "umount /fat");

  free(oldl);
  free(newl);

  if(failed){
    printf("fat32test: FAILED\n");
    exit(1);
  }
  printf("fat32test: ALL PASSED\n");
  exit(0);
}
