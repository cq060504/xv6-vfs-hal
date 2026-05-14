#include "types.h"
#include "sleeplock.h"

struct vnode;
struct mount;

struct vnode_ops {
  int (*lookup)(struct vnode*, char*, struct vnode**);
  int (*mknod)(struct vnode*, char*, int, int);
  int (*read)(struct vnode*, uint64, int, uint);
  int (*write)(struct vnode*, uint64, int, uint);
  int (*stat)(struct vnode*, uint64);
  int (*readdir)(struct vnode*, uint64, uint);
  int (*create)(struct vnode*, char*, short, struct vnode**);
  int (*unlink)(struct vnode*, char*);
  int (*mkdir)(struct vnode*, char*);
  int (*link)(struct vnode*, char*, struct vnode*);
  void (*destroy)(void*);
};

struct vfs_ops {
  struct vnode* (*root)(struct mount*);
};

struct vnode {
  uint type;
  uint dev;
  int major;
  int minor;
  struct vnode_ops *ops;
  struct mount *mp;
  int ref;
  struct sleeplock lock;
  uint inum;
  void *priv;
};

struct mount {
  struct vfs_ops *ops;
  struct vnode *root;
  uint dev;
  char path[128];
  void *priv;
};

#define V_FILE 1
#define V_DIR  2
#define V_DEV  3

#define NMOUNT 8
#define NVNODE 200

#define VOP_LOOKUP(v,n,r)    (v)->ops->lookup(v,n,r)
#define VOP_READ(v,a,n,o)    (v)->ops->read(v,a,n,o)
#define VOP_WRITE(v,a,n,o)   (v)->ops->write(v,a,n,o)
#define VOP_STAT(v,a)        (v)->ops->stat(v,a)
#define VOP_READDIR(v,a,o)   (v)->ops->readdir(v,a,o)
#define VOP_CREATE(v,n,t,p)  (v)->ops->create(v,n,t,p)
#define VOP_UNLINK(v,n)      (v)->ops->unlink(v,n)
#define VOP_MKDIR(v,n)       (v)->ops->mkdir(v,n)
#define VOP_LINK(v,n,o)      (v)->ops->link(v,n,o)
#define VOP_MKNOD(v,n,ma,mi) (v)->ops->mknod(v,n,ma,mi)
#define VFS_ROOT(m)          (m)->ops->root(m)

void          vfs_init(void);
struct vnode* vfs_root(void);

struct vnode* alloc_vnode(void);
void          free_vnode(struct vnode*);
struct vnode* vget(struct vnode*);
void          vput(struct vnode*);
void          vn_lock(struct vnode*);
void          vn_unlock(struct vnode*);

struct mount* vfs_mount(char*, uint, char*);
struct mount* vfs_lookup_mount(char*, char**);

struct vnode* vfs_namei(char*);
struct vnode* vfs_nameiparent(char*, char*);

int           vfs_open(char*, int, struct vnode**);
int           vfs_read(struct vnode*, uint64, int, uint);
int           vfs_write(struct vnode*, uint64, int, uint);
int           vfs_readdir(struct vnode*, uint64, uint);
int           vfs_close(struct vnode*);
int           vfs_create(struct vnode*, char*, short, struct vnode**);
int           vfs_unlink(struct vnode*, char*);
int           vfs_mkdir(struct vnode*, char*);
int           vfs_link(struct vnode*, struct vnode*, char*);
int           vfs_stat(struct vnode*, uint64);
int           vfs_mknod(struct vnode*, char*, int, int);

void          vfs_register(struct mount* (*)(uint), char*);
struct mount* xv6fs_mount(uint);
