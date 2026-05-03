#include "types.h"

struct vfs;
struct vnode;

struct vfsops {
  struct vnode* (*root)(struct vfs*);
  struct vnode* (*lookup)(struct vnode*, char*);
};

struct vnops {
  int (*read)(struct vnode*, uint64, int, uint64);
  int (*write)(struct vnode*, uint64, int, uint64);
  int (*stat)(struct vnode*, uint64);
};

struct vfs {
  struct vfsops *ops;
  struct vnode *root;
  void *priv;
};

struct vnode {
  uint type;
  struct vnops *ops;
  struct vfs *vfsp;
  int ref;
  void *priv;
};

#define VOP_READ(v,a,n,o)  (v)->ops->read(v,a,n,o)
#define VOP_WRITE(v,a,n,o) (v)->ops->write(v,a,n,o)
#define VOP_STAT(v,a)      (v)->ops->stat(v,a)
#define VFS_ROOT(v)        (v)->ops->root(v)

void          vfs_init(int);
struct vnode* vfs_root(void);
struct vnode* vnamei(char*);
