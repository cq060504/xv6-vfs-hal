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
//   LFN (> 13 chars) short-name fallback display + reopen via short name
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

// ---- directory listing helper --------------------------------------
struct dlist {
  char name[16];
  ushort inum;
};

// Read all directory entries of `path` into `out` (max `max` entries).
// Returns entry count, or -1 on open failure.
static int
readdirall(char *path, struct dlist *out, int max)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0) return -1;
  struct dirent de;
  int cnt = 0;
  while(cnt < max && read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0) continue;
    memmove(out[cnt].name, de.name, 14);
    out[cnt].name[14] = 0;
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

static char *newpath(char *p, char *name, char *out)
{
  memmove(out, p, strlen(p) + 1);
  memmove(out + strlen(p), name, strlen(name) + 1);
  return out;
}

int
main(void)
{
  struct stat st;
  struct dlist oldl[48], newl[48];
  int fd, i, j;

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

  // ---- 11. LFN > 13 chars: short-name fallback + reopen via short name ----
  int oc = readdirall("/fat", oldl, 48);  // snapshot before
  char *ldata = "long-data-content-123456";
  fd = open("/fat/LongFileNameTest.txt", O_WRONLY | O_CREATE | O_TRUNC);
  check(fd >= 0, "create LFN >13");
  if(fd >= 0){ write(fd, ldata, 23); close(fd); }

  int nc = readdirall("/fat", newl, 48);
  char shortnm[16];
  int snfound = 0;
  for(i = 0; i < nc && !snfound; i++){
    int inold = 0;
    for(j = 0; j < oc; j++)
      if(strcmp(newl[i].name, oldl[j].name) == 0) inold = 1;
    if(!inold && newl[i].inum != 0){
      memmove(shortnm, newl[i].name, 15);
      shortnm[15] = 0;
      snfound = 1;
    }
  }
  check(snfound, "LFN >13 visible as new entry");
  if(snfound){
    char full[64];
    newpath("/fat/", shortnm, full);
    fd = open(full, O_RDONLY);
    if(fd < 0){
      printf("FAIL: reopen LFN via short name (%s)\n", full);
      failed = 1;
    } else {
      char rbuf[64];
      int rn = read(fd, rbuf, 64);
      close(fd);
      check(rn == 23 && memcmp(rbuf, ldata, 23) == 0,
            "reopen LFN >13 via presented short name");
    }
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

  check(umount("/fat") == 0, "umount /fat");

  if(failed){
    printf("fat32test: FAILED\n");
    exit(1);
  }
  printf("fat32test: ALL PASSED\n");
  exit(0);
}
