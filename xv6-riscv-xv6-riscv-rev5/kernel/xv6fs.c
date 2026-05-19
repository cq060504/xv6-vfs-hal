#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "buf.h"
#include "vfs.h"

extern struct superblock sb;

static struct vnode_ops xv6fs_vnops;
static struct vfs_ops xv6fs_vfsops;

// VFS-level dirent returned by readdir (must match vfs.h definition)
struct vdirent {
  ushort inum;
  char name[14];
};

struct xv6fs_vnode_priv {
  struct inode *ip;
};

static void
xv6fs_destroy(void *arg)
{
  struct xv6fs_vnode_priv *priv = (struct xv6fs_vnode_priv*)arg;
  if(priv){
    if(priv->ip){
      begin_op();
      iput(priv->ip);
      end_op();
    }
    kfree((void*)priv);
  }
}

static struct vnode*
xv6fs_vnode_from_inode(struct inode *ip, uint dev)
{
  struct vnode *vp = alloc_vnode();
  struct xv6fs_vnode_priv *priv;

  if(vp == 0) return 0;

  priv = (struct xv6fs_vnode_priv*)kalloc();
  if(priv == 0){
    vput(vp);
    return 0;
  }
  priv->ip = ip;

  if(ip->type == T_DIR)
    vp->type = V_DIR;
  else if(ip->type == T_DEVICE)
    vp->type = V_DEV;
  else
    vp->type = V_FILE;
  vp->major = ip->major;
  vp->minor = ip->minor;
  vp->dev = dev;
  vp->ops = &xv6fs_vnops;
  vp->priv = priv;
  vp->inum = ip->inum;
  return vp;
}

struct mount*
xv6fs_mount(uint dev)
{
  struct mount *mp;
  struct inode *ip;

  mp = (struct mount*)kalloc();
  if(mp == 0) return 0;
  mp->ops = &xv6fs_vfsops;
  mp->dev = dev;
  mp->priv = 0;

  // fsinit(dev) already called by forkret() before vfs_mount()
  // If vfs_mount is called elsewhere, caller must ensure fsinit done.

  ip = iget(dev, ROOTINO);
  ilock(ip);
  mp->root = xv6fs_vnode_from_inode(ip, dev);
  iunlock(ip);
  if(mp->root == 0){
    kfree((void*)mp);
    return 0;
  }
  return mp;
}

static struct vnode*
xv6fs_root(struct mount *mp)
{
  return vget(mp->root);
}

static int
xv6fs_lookup(struct vnode *dir, char *name, struct vnode **result)
{
  struct xv6fs_vnode_priv *dpriv = (struct xv6fs_vnode_priv*)dir->priv;
  struct inode *dp = dpriv->ip;
  struct inode *ip;

  ilock(dp);
  if(dp->type != T_DIR){
    iunlock(dp);
    return -1;
  }
  ip = dirlookup(dp, name, 0);
  iunlock(dp);

  if(ip == 0) return -1;

  ilock(ip);
  *result = xv6fs_vnode_from_inode(ip, dir->dev);
  iunlock(ip);
  if(*result == 0) return -1;
  return 0;
}

static int
xv6fs_read(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct xv6fs_vnode_priv *priv = (struct xv6fs_vnode_priv*)vp->priv;
  struct inode *ip = priv->ip;
  ilock(ip);
  int r = readi(ip, 1, buf, off, n);
  iunlock(ip);
  return r;
}

static int
xv6fs_write(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct xv6fs_vnode_priv *priv = (struct xv6fs_vnode_priv*)vp->priv;
  struct inode *ip = priv->ip;
  int r, max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
  int i = 0;
  while(i < n){
    int n1 = n - i;
    if(n1 > max) n1 = max;
    begin_op();
    ilock(ip);
    if((r = writei(ip, 1, buf + i, off + i, n1)) > 0)
      i += r;
    iunlock(ip);
    end_op();
    if(r != n1) break;
  }
  return (i == n ? n : -1);
}

static int
xv6fs_stat(struct vnode *vp, uint64 addr)
{
  struct xv6fs_vnode_priv *priv = (struct xv6fs_vnode_priv*)vp->priv;
  struct proc *p = myproc();
  struct stat st;
  struct inode *ip = priv->ip;

  ilock(ip);
  stati(ip, &st);
  iunlock(ip);

  if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

static int
xv6fs_readdir(struct vnode *vp, uint64 buf, uint off)
{
  struct xv6fs_vnode_priv *priv = (struct xv6fs_vnode_priv*)vp->priv;
  struct inode *ip = priv->ip;
  struct dirent de;
  struct vdirent vde;
  int r;

  ilock(ip);
  if(ip->type != T_DIR){
    iunlock(ip);
    return -1;
  }

  for(;;){
    r = readi(ip, 0, (uint64)&de, off, sizeof(de));
    if(r != sizeof(de)){
      iunlock(ip);
      return -1;
    }
    off += sizeof(de);
    if(de.inum != 0)
      break;
  }
  iunlock(ip);

  vde.inum = de.inum;
  safestrcpy(vde.name, de.name, 14);

  if(copyout(myproc()->pagetable, buf, (char*)&vde, sizeof(vde)) < 0)
    return -1;
  return off;
}

static int
isdirempty(struct inode *dp)
{
  struct dirent de;
  uint off;

  for(off = 2 * sizeof(de); off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

static int
xv6fs_create(struct vnode *dir, char *name, short type, struct vnode **new)
{
  struct xv6fs_vnode_priv *dpriv = (struct xv6fs_vnode_priv*)dir->priv;
  struct inode *dp = dpriv->ip;
  struct inode *ip;

  begin_op();

  ilock(dp);
  if(dp->type != T_DIR){
    iunlock(dp);
    end_op();
    return -1;
  }
  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlock(dp);
    iput(ip);
    end_op();
    return -1;
  }

  ip = ialloc(dir->dev, (type == V_DIR) ? T_DIR : T_FILE);
  if(ip == 0){
    iunlock(dp);
    end_op();
    return -1;
  }

  ilock(ip);
  ip->nlink = 1;
  ip->size = 0;
  iupdate(ip);
  iunlock(ip);

  if(dirlink(dp, name, ip->inum) < 0){
    iunlock(dp);
    iput(ip);
    end_op();
    return -1;
  }

  if(type == V_DIR){
    ilock(ip);
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0){
      // Undo the name entry we just added in dp
      uint off;
      struct inode *tmp;
      struct dirent de;
      if((tmp = dirlookup(dp, name, &off)) == ip){
        memset(&de, 0, sizeof(de));
        writei(dp, 0, (uint64)&de, off, sizeof(de));
      }
      iunlock(ip);
      iunlock(dp);
      iput(ip);
      end_op();
      return -1;
    }
    iunlock(ip);
    dp->nlink++;
    iupdate(dp);
  }

  iunlock(dp);
  end_op();

  if(new){
    ilock(ip);
    *new = xv6fs_vnode_from_inode(ip, dir->dev);
    iunlock(ip);
    if(*new == 0) return -1;
  }
  return 0;
}

static int
xv6fs_unlink(struct vnode *dir, char *name)
{
  struct xv6fs_vnode_priv *dpriv = (struct xv6fs_vnode_priv*)dir->priv;
  struct inode *dp = dpriv->ip;
  struct inode *ip;
  uint off;
  struct dirent de;

  begin_op();
  ilock(dp);
  if(dp->type != T_DIR){
    iunlock(dp);
    end_op();
    return -1;
  }
  if((ip = dirlookup(dp, name, &off)) == 0){
    iunlock(dp);
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->nlink < 1) panic("unlink: nlink < 1");
  int is_dir = (ip->type == T_DIR);
  if(is_dir && !isdirempty(ip)){
    iunlock(ip);
    iunlock(dp);
    iput(ip);
    end_op();
    return -1;
  }
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");

  if(is_dir){
    dp->nlink--;
    iupdate(dp);
  }

  iunlock(dp);
  end_op();
  return 0;
}

static int
xv6fs_mkdir(struct vnode *dir, char *name)
{
  struct vnode *new = 0;
  int r = xv6fs_create(dir, name, V_DIR, &new);
  if(r == 0 && new) vput(new);
  return r;
}

static int
xv6fs_link(struct vnode *dir, char *name, struct vnode *old)
{
  struct xv6fs_vnode_priv *dpriv = (struct xv6fs_vnode_priv*)dir->priv;
  struct xv6fs_vnode_priv *opriv = (struct xv6fs_vnode_priv*)old->priv;
  struct inode *dp = dpriv->ip;
  struct inode *ip = opriv->ip;
  int r;

  begin_op();

  // Lock old inode, increment nlink (single lock, like xv6 original)
  ilock(ip);
  if(ip->type == T_DIR){
    iunlock(ip);
    end_op();
    return -1;
  }
  if(ip->dev != dp->dev){
    iunlock(ip);
    end_op();
    return -1;
  }
  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  // Lock parent, add dir entry
  ilock(dp);
  r = dirlink(dp, name, ip->inum);
  iunlock(dp);

  // Rollback on failure
  if(r < 0){
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlock(ip);
  }

  end_op();
  return r;
}

static int
xv6fs_mknod(struct vnode *dir, char *name, int major, int minor)
{
  struct vnode *new = 0;
  struct xv6fs_vnode_priv *dpriv = (struct xv6fs_vnode_priv*)dir->priv;
  struct inode *dp = dpriv->ip;
  struct inode *ip;

  begin_op();
  ilock(dp);
  if(dp->type != T_DIR){
    iunlock(dp);
    end_op();
    return -1;
  }
  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlock(dp);
    iput(ip);
    end_op();
    return -1;
  }
  ip = ialloc(dir->dev, T_DEVICE);
  if(ip == 0){
    iunlock(dp);
    end_op();
    return -1;
  }
  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);
  iunlock(ip);
  if(dirlink(dp, name, ip->inum) < 0){
    iunlock(dp);
    iput(ip);
    end_op();
    return -1;
  }
  iunlock(dp);
  end_op();

  if(new) vput(new);
  return 0;
}

static int
xv6fs_read_kernel(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct xv6fs_vnode_priv *priv = (struct xv6fs_vnode_priv*)vp->priv;
  struct inode *ip = priv->ip;
  ilock(ip);
  int r = readi(ip, 0, buf, off, n);
  iunlock(ip);
  return r;
}

static int
xv6fs_truncate(struct vnode *vp)
{
  struct xv6fs_vnode_priv *priv = (struct xv6fs_vnode_priv*)vp->priv;
  struct inode *ip = priv->ip;
  begin_op();
  ilock(ip);
  itrunc(ip);
  iunlock(ip);
  end_op();
  return 0;
}

static struct vnode_ops xv6fs_vnops = {
  .lookup      = xv6fs_lookup,
  .mknod       = xv6fs_mknod,
  .read        = xv6fs_read,
  .write       = xv6fs_write,
  .stat        = xv6fs_stat,
  .readdir     = xv6fs_readdir,
  .create      = xv6fs_create,
  .unlink      = xv6fs_unlink,
  .mkdir       = xv6fs_mkdir,
  .link        = xv6fs_link,
  .read_kernel = xv6fs_read_kernel,
  .truncate    = xv6fs_truncate,
  .destroy     = xv6fs_destroy,
};

static struct vfs_ops xv6fs_vfsops = {
  .root = xv6fs_root,
};
