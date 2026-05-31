//
// File-system system calls.
// Uses the VFS layer instead of direct inode operations.
//

#include "types.h"
#include "hal/hal.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "vfs.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st;

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[14], new[MAXPATH], old[MAXPATH];
  struct vnode *ip, *dp;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  if((ip = vfs_namei(old)) == 0)
    return -1;

  if(ip->type == V_DIR){
    vput(ip);
    return -1;
  }

  if((dp = vfs_nameiparent(new, name)) == 0){
    vput(ip);
    return -1;
  }

  int r = vfs_link(ip, dp, name);
  vput(dp);
  vput(ip);
  return r;
}

uint64
sys_unlink(void)
{
  char name[14], path[MAXPATH];
  struct vnode *dp;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  if(namecmp(path, "/") == 0)
    return -1;

  if((dp = vfs_nameiparent(path, name)) == 0)
    return -1;

  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0){
    vput(dp);
    return -1;
  }

  int r = vfs_unlink(dp, name);
  vput(dp);
  return r;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct vnode *vp;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  if(vfs_open(path, omode, &vp) < 0)
    return -1;

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f) fileclose(f);
    vput(vp);
    return -1;
  }

  if(vp->type == V_DEV){
    if(vp->major < 0 || vp->major >= NDEV){
      vput(vp);
      fileclose(f);
      return -1;
    }
    f->type = FD_DEVICE;
    f->major = vp->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }

  // O_TRUNC: truncate regular file to zero bytes
  if((omode & O_TRUNC) && vp->type == V_FILE){
    vfs_truncate(vp);
  }

  f->vnode = vp;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  f->appendable = (omode & O_APPEND) ? 1 : 0;

  // L4.1: O_APPEND sets initial offset to current file size
  if(f->appendable && vp->type == V_FILE)
    f->off = vp->size;

  return fd;
}

uint64
sys_mkdir(void)
{
  char name[14], path[MAXPATH];
  struct vnode *dp;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  if((dp = vfs_nameiparent(path, name)) == 0)
    return -1;

  int r = vfs_mkdir(dp, name);
  vput(dp);
  return r;
}

uint64
sys_mknod(void)
{
  char name[14], path[MAXPATH];
  int major, minor;
  struct vnode *dp;

  argint(1, &major);
  argint(2, &minor);
  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  if((dp = vfs_nameiparent(path, name)) == 0)
    return -1;

  int r = vfs_mknod(dp, name, major, minor);
  vput(dp);
  return r;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct vnode *ip;
  struct proc *p = myproc();

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  if((ip = vfs_namei(path)) == 0)
    return -1;

  if(ip->type != V_DIR){
    vput(ip);
    return -1;
  }

  vput(p->cwd);
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_mount(void)
{
  int dev;
  char path[MAXPATH], fstype[16];

  if(argstr(0, path, MAXPATH) < 0)
    return -1;
  argint(1, &dev);
  if(argstr(2, fstype, sizeof(fstype)) < 0)
    return -1;

  if(vfs_mount(path, dev, fstype) == 0)
    return -1;
  return 0;
}

uint64
sys_umount(void)
{
  char path[MAXPATH];

  if(argstr(0, path, MAXPATH) < 0)
    return -1;
  return vfs_umount(path);
}

uint64
sys_pipe(void)
{
  uint64 fdarray;
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
