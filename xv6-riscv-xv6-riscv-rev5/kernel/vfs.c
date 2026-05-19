#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "proc.h"
#include "fcntl.h"
#include "vfs.h"

struct mount *mounttable[NMOUNT];
int nmount;

struct vnode vnode_table[NVNODE];
struct spinlock vnode_lock;

struct {
  struct mount* (*mountfn)(uint);
  char name[16];
} fstypes[8];
int nfstype;

void
vfs_init(void)
{
  int i;
  initlock(&vnode_lock, "vnode_lock");
  for(i = 0; i < NVNODE; i++)
    initsleeplock(&vnode_table[i].lock, "vnode");
  nmount = 0;
  nfstype = 0;
  vfs_register(xv6fs_mount, "xv6fs");
}

void
vfs_register(struct mount* (*fn)(uint), char *name)
{
  if(nfstype >= 8) panic("vfs_register: too many fstypes");
  fstypes[nfstype].mountfn = fn;
  safestrcpy(fstypes[nfstype].name, name, 16);
  nfstype++;
}

struct vnode*
vfs_root(void)
{
  if(nmount == 0) return 0;
  return vget(mounttable[0]->root);
}

struct mount*
vfs_mount(char *path, uint dev, char *fstype)
{
  int i;
  struct mount *mp = 0;

  for(i = 0; i < nfstype; i++){
    if(strncmp(fstypes[i].name, fstype, 16) == 0){
      mp = fstypes[i].mountfn(dev);
      break;
    }
  }
  if(mp == 0) return 0;

  safestrcpy(mp->path, path, 128);
  mp->dev = dev;

  if(nmount >= NMOUNT) return 0;
  mounttable[nmount] = mp;
  nmount++;
  return mp;
}

struct mount*
vfs_lookup_mount(char *path, char **rest)
{
  int i, best = -1, bestlen = 0;
  for(i = 0; i < nmount; i++){
    int len = strlen(mounttable[i]->path);
    if(len > bestlen && strncmp(mounttable[i]->path, path, len) == 0){
      if(len == 1 || path[len] == '/' || path[len] == '\0'){
        best = i;
        bestlen = len;
      }
    }
  }
  if(best < 0) return 0;
  *rest = path + bestlen;
  if(**rest == '/') (*rest)++;
  return mounttable[best];
}

struct vnode*
alloc_vnode(void)
{
  int i;
  acquire(&vnode_lock);
  for(i = 0; i < NVNODE; i++){
    if(vnode_table[i].ref == 0){
      vnode_table[i].ref = 1;
      vnode_table[i].type = 0;
      vnode_table[i].ops = 0;
      vnode_table[i].mp = 0;
      vnode_table[i].priv = 0;
      vnode_table[i].inum = 0;
      vnode_table[i].major = 0;
      vnode_table[i].minor = 0;
      release(&vnode_lock);
      return &vnode_table[i];
    }
  }
  release(&vnode_lock);
  return 0;
}

struct vnode*
vget(struct vnode *vp)
{
  acquire(&vnode_lock);
  vp->ref++;
  release(&vnode_lock);
  return vp;
}

void
vput(struct vnode *vp)
{
  void *priv;
  void (*destroy)(void*);
  int need_free = 0;

  acquire(&vnode_lock);
  if(vp->ref < 1) panic("vput: ref underflow");
  vp->ref--;
  if(vp->ref == 0){
    need_free = 1;
    priv = vp->priv;
    destroy = (vp->ops) ? vp->ops->destroy : 0;
    vp->priv = 0;
    vp->ops = 0;
    vp->mp = 0;
    vp->type = 0;
  }
  release(&vnode_lock);

  // Must call destroy outside spinlock (may sleep)
  if(need_free && destroy)
    destroy(priv);
}

void
free_vnode(struct vnode *vp)
{
  // free_vnode is only called when we know ref is already 0
  // and we hold no lock. Just call vput which handles cleanup.
  vput(vp);
}

void
vn_lock(struct vnode *vp)
{
  acquiresleep(&vp->lock);
}

void
vn_unlock(struct vnode *vp)
{
  releasesleep(&vp->lock);
}

// Resolve relative/absolute paths.
// For absolute paths: find mount, start from root.
// For relative paths: start from cwd.
static struct vnode*
path_start(char *path, char **rest)
{
  struct mount *mp;

  if(*path == '/'){
    mp = vfs_lookup_mount(path, rest);
    if(mp == 0) return 0;
    return vget(mp->root);
  }
  // Relative path: start from cwd
  struct proc *p = myproc();
  if(p->cwd == 0) return 0;
  *rest = path;
  return vget(p->cwd);
}

static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/') path++;
  if(*path == 0) return 0;
  s = path;
  while(*path != '/' && *path != 0) path++;
  len = path - s;
  if(len >= 14) len = 14;
  memmove(name, s, len);
  name[len] = 0;
  while(*path == '/') path++;
  return path;
}

struct vnode*
vfs_namei(char *path)
{
  char name[15];
  struct vnode *vp, *next;
  struct mount *submp;
  char *rest;

  vp = path_start(path, &rest);
  if(vp == 0) return 0;

  while((rest = skipelem(rest, name)) != 0){
    vn_lock(vp);
    if(vp->type != V_DIR){
      vn_unlock(vp); vput(vp); return 0;
    }
    if(vp->ops == 0 || vp->ops->lookup == 0){
      vn_unlock(vp); vput(vp); return 0;
    }
    if(vp->ops->lookup(vp, name, &next) != 0){
      vn_unlock(vp); vput(vp); return 0;
    }
    vn_unlock(vp);
    vput(vp);

    // Check if we crossed a mount point
    // Build the full path so far and look for a mount
    // In practice, we check if the resulting vnode is a mount root
    // A simpler approach: just use vget on the result
    submp = 0;
    for(int i = 1; i < nmount; i++){
      if(mounttable[i]->root == next){
        submp = mounttable[i];
        break;
      }
    }
    if(submp){
      vput(next);
      vp = vget(submp->root);
    } else {
      vp = next;
    }
  }
  return vp;
}

struct vnode*
vfs_nameiparent(char *path, char *name)
{
  char buf[15];
  struct vnode *vp, *next;
  struct mount *submp;
  char *rest;

  vp = path_start(path, &rest);
  if(vp == 0) return 0;

  while((rest = skipelem(rest, buf)) != 0){
    if(*rest == '\0'){
      safestrcpy(name, buf, 15);
      if(vp->type != V_DIR){
        vput(vp);
        return 0;
      }
      return vp;
    }
    vn_lock(vp);
    if(vp->type != V_DIR){
      vn_unlock(vp); vput(vp); return 0;
    }
    if(vp->ops == 0 || vp->ops->lookup == 0){
      vn_unlock(vp); vput(vp); return 0;
    }
    if(vp->ops->lookup(vp, buf, &next) != 0){
      vn_unlock(vp); vput(vp); return 0;
    }
    vn_unlock(vp);
    vput(vp);

    submp = 0;
    for(int i = 1; i < nmount; i++){
      if(mounttable[i]->root == next){
        submp = mounttable[i];
        break;
      }
    }
    if(submp){
      vput(next);
      vp = vget(submp->root);
    } else {
      vp = next;
    }
  }
  vput(vp);
  return 0;
}

int
vfs_open(char *path, int mode, struct vnode **vp)
{
  struct vnode *ip;
  if((ip = vfs_namei(path)) == 0){
    if(mode & O_CREATE){
      char name[15];
      struct vnode *dir = vfs_nameiparent(path, name);
      if(dir == 0) return -1;
      vn_lock(dir);
      int r = VOP_CREATE(dir, name, V_FILE, &ip);
      vn_unlock(dir);
      vput(dir);
      if(r != 0) return -1;
    } else {
      return -1;
    }
  }
  // O_CREATE on an existing directory should fail
  if((mode & O_CREATE) && ip->type == V_DIR){
    vput(ip);
    return -1;
  }
  // Reject write-open on directories (O_WRONLY or O_RDWR)
  if(ip->type == V_DIR && (mode & (O_WRONLY | O_RDWR))){
    vput(ip);
    return -1;
  }
  *vp = ip;
  return 0;
}

int
vfs_read(struct vnode *vp, uint64 buf, int n, uint off)
{
  if(vp->ops == 0 || vp->ops->read == 0) return -1;
  return VOP_READ(vp, buf, n, off);
}

int
vfs_write(struct vnode *vp, uint64 buf, int n, uint off)
{
  if(vp->ops == 0 || vp->ops->write == 0) return -1;
  return VOP_WRITE(vp, buf, n, off);
}

int
vfs_readdir(struct vnode *vp, uint64 buf, uint off)
{
  if(vp->ops == 0 || vp->ops->readdir == 0) return -1;
  return VOP_READDIR(vp, buf, off);
}

int
vfs_close(struct vnode *vp)
{
  vput(vp);
  return 0;
}

int
vfs_create(struct vnode *dir, char *name, short type, struct vnode **new)
{
  if(dir->type != V_DIR) return -1;
  if(dir->ops == 0 || dir->ops->create == 0) return -1;
  vn_lock(dir);
  int r = VOP_CREATE(dir, name, type, new);
  vn_unlock(dir);
  return r;
}

int
vfs_unlink(struct vnode *dir, char *name)
{
  if(dir->type != V_DIR) return -1;
  if(dir->ops == 0 || dir->ops->unlink == 0) return -1;
  vn_lock(dir);
  int r = VOP_UNLINK(dir, name);
  vn_unlock(dir);
  return r;
}

int
vfs_mkdir(struct vnode *dir, char *name)
{
  if(dir->type != V_DIR) return -1;
  if(dir->ops == 0 || dir->ops->mkdir == 0) return -1;
  vn_lock(dir);
  int r = VOP_MKDIR(dir, name);
  vn_unlock(dir);
  return r;
}

int
vfs_link(struct vnode *old, struct vnode *newdir, char *name)
{
  if(newdir->type != V_DIR) return -1;
  if(newdir->ops == 0 || newdir->ops->link == 0) return -1;
  vn_lock(newdir);
  int r = VOP_LINK(newdir, name, old);
  vn_unlock(newdir);
  return r;
}

int
vfs_stat(struct vnode *vp, uint64 addr)
{
  if(vp->ops == 0 || vp->ops->stat == 0) return -1;
  return VOP_STAT(vp, addr);
}

int
vfs_mknod(struct vnode *dir, char *name, int major, int minor)
{
  if(dir->type != V_DIR) return -1;
  if(dir->ops == 0 || dir->ops->mknod == 0) return -1;
  vn_lock(dir);
  int r = VOP_MKNOD(dir, name, major, minor);
  vn_unlock(dir);
  return r;
}

int
vfs_read_kernel(struct vnode *vp, uint64 buf, int n, uint off)
{
  if(vp->ops == 0 || vp->ops->read_kernel == 0) return -1;
  return VOP_READ_KERNEL(vp, buf, n, off);
}

int
vfs_truncate(struct vnode *vp)
{
  if(vp->ops == 0 || vp->ops->truncate == 0) return -1;
  if(vp->type != V_FILE) return -1;
  return VOP_TRUNCATE(vp);
}
