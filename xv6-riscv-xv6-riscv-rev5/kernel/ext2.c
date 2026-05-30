// ext2 filesystem implementation for xv6 VFS.
//
// Assumes block_size == 1024 (BSIZE).  Only revision 0 basic format
// (no extents, no journal).  Single block group only (for now).

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "stat.h"
#include "fs.h"
#include "buf.h"
#include "vfs.h"
#include "ext2.h"

static struct vnode_ops ext2_vnops;
static struct vfs_ops  ext2_vfsops;

// Per-mount private data
struct ext2_mount_priv {
  struct ext2_superblock sb;
  struct ext2_bgdesc     bg;
  uint                   dev;
  struct sleeplock       lock;   // serialize meta-data allocations (balloc/ialloc)
  struct mount          *mnt;    // back-pointer to VFS mount (set by ext2_mount)
};

// Per-vnode private data
struct ext2_vnode_priv {
  struct ext2_inode_disk ino;
  struct ext2_mount_priv *mp;
  uint  inum;
  int   dirty;
};

// ---------------------------------------------------------------------------
// read / write on-disk inode
// ---------------------------------------------------------------------------
static uint
ino_block(struct ext2_mount_priv *mp, uint inum)
{
  uint blk = mp->bg.bg_inode_table + (inum - 1) / (BSIZE / sizeof(struct ext2_inode_disk));
  return blk;
}

static uint
ino_offset(uint inum)
{
  return ((inum - 1) % (BSIZE / sizeof(struct ext2_inode_disk))) * sizeof(struct ext2_inode_disk);
}

static int
read_inode(struct ext2_mount_priv *mp, uint inum, struct ext2_inode_disk *ino)
{
  struct buf *bp = bread(mp->dev, ino_block(mp, inum));
  if(bp == 0) return -1;
  memmove(ino, bp->data + ino_offset(inum), sizeof(struct ext2_inode_disk));
  brelse(bp);
  return 0;
}

static int
write_inode(struct ext2_mount_priv *mp, uint inum, struct ext2_inode_disk *ino)
{
  struct buf *bp = bread(mp->dev, ino_block(mp, inum));
  if(bp == 0) return -1;
  memmove(bp->data + ino_offset(inum), ino, sizeof(struct ext2_inode_disk));
  bwrite(bp);
  brelse(bp);
  return 0;
}

// ---------------------------------------------------------------------------
// timestamp helper (B6: update i_atime/i_mtime/i_ctime)
// ---------------------------------------------------------------------------
static void
ext2_update_time(struct ext2_vnode_priv *priv, int access, int mod)
{
  uint t = ticks;
  if(access) priv->ino.i_atime = t;
  if(mod){
    priv->ino.i_mtime = t;
    priv->ino.i_ctime = t;
  }
  if(access || mod) priv->dirty = 1;
}

// ---------------------------------------------------------------------------
// vnode pool (protected by vnode_lock in vfs.c)
// ---------------------------------------------------------------------------
static struct vnode*
ext2_vnode_alloc(struct ext2_mount_priv *mp, uint inum, struct ext2_inode_disk *ino)
{
  struct vnode *vp = alloc_vnode();
  struct ext2_vnode_priv *priv;
  if(vp == 0) return 0;

  priv = (struct ext2_vnode_priv*)kalloc();
  if(priv == 0){ vput(vp); return 0; }
  memmove(&priv->ino, ino, sizeof(struct ext2_inode_disk));
  priv->mp  = mp;
  priv->inum = inum;
  priv->dirty = 0;

  ushort mode = ino->i_mode & 0xF000;
  if(mode == 0x4000)      vp->type = V_DIR;
  else if(mode == 0x2000 || mode == 0x6000) vp->type = V_DEV;
  else if(mode == 0x8000) vp->type = V_FILE;
  else                    vp->type = V_FILE;  // default (symlink/socket treated as file)

  // B5: recover major/minor from i_block[0] for device nodes
  if(vp->type == V_DEV){
    vp->major = (ino->i_block[0] >> 8) & 0xFF;
    vp->minor = ino->i_block[0] & 0xFF;
  } else {
    vp->major = 0;
    vp->minor = 0;
  }

  vp->dev   = mp->dev;
  vp->mp    = mp->mnt;
  vp->ops   = &ext2_vnops;
  vp->priv  = priv;
  vp->inum  = inum;
  return vp;
}

static void
ext2_destroy(void *arg)
{
  struct ext2_vnode_priv *priv = (struct ext2_vnode_priv*)arg;
  if(priv == 0) return;
  if(priv->dirty && priv->mp)
    write_inode(priv->mp, priv->inum, &priv->ino);
  kfree((void*)priv);
}

// ---------------------------------------------------------------------------
// iget  --  get a vnode by inode number (with ref)
// ---------------------------------------------------------------------------
static struct vnode*
ext2_iget(struct ext2_mount_priv *mp, uint inum)
{
  struct ext2_inode_disk ino;
  if(read_inode(mp, inum, &ino) < 0) return 0;
  return ext2_vnode_alloc(mp, inum, &ino);
}

// ---------------------------------------------------------------------------
// bitmap helpers (single block group, bitmap fits in one block)
// ---------------------------------------------------------------------------

// Allocate a free bit from bitmap block `bb`, bits 1..nbits-1 (bit 0 unused)
static int
bitmap_alloc(struct ext2_mount_priv *mp, uint bb, uint nbits)
{
  struct buf *bp = bread(mp->dev, bb);
  if(bp == 0) return -1;
  acquiresleep(&mp->lock);
  for(uint i = 1; i < nbits && i < BSIZE*8; i++){
    if((bp->data[i / 8] & (1 << (i % 8))) == 0){
      bp->data[i / 8] |= (1 << (i % 8));
      bwrite(bp);
      releasesleep(&mp->lock);
      brelse(bp);
      return i;
    }
  }
  releasesleep(&mp->lock);
  brelse(bp);
  return -1;
}

// B1: bitmap_free now acquires the same lock as bitmap_alloc
static void
bitmap_free(struct ext2_mount_priv *mp, uint bb, uint n)
{
  struct buf *bp = bread(mp->dev, bb);
  if(bp == 0) return;
  acquiresleep(&mp->lock);
  bp->data[n / 8] &= ~(1 << (n % 8));
  bwrite(bp);
  releasesleep(&mp->lock);
  brelse(bp);
}

// Write superblock out to disk.  Caller must hold mp->lock.
static void
ext2_sb_write(struct ext2_mount_priv *mp)
{
  struct buf *sbp = bread(mp->dev, 1);
  if(sbp){
    memmove(sbp->data, &mp->sb, sizeof(mp->sb));
    bwrite(sbp);
    brelse(sbp);
  }
}

// ---------------------------------------------------------------------------
// balloc / bfree
// ---------------------------------------------------------------------------
static uint
ext2_balloc(struct ext2_mount_priv *mp)
{
  int b = bitmap_alloc(mp, mp->bg.bg_block_bitmap, mp->sb.s_blocks_count);
  if(b < 0) return 0;
  acquiresleep(&mp->lock);
  mp->sb.s_free_blocks_count--;
  ext2_sb_write(mp);
  releasesleep(&mp->lock);
  return (uint)b;
}

static void
ext2_bfree(struct ext2_mount_priv *mp, uint blockno)
{
  bitmap_free(mp, mp->bg.bg_block_bitmap, blockno);
  acquiresleep(&mp->lock);
  mp->sb.s_free_blocks_count++;
  ext2_sb_write(mp);
  releasesleep(&mp->lock);
}

// ---------------------------------------------------------------------------
// ialloc / ifree
// ---------------------------------------------------------------------------
static uint
ext2_ialloc(struct ext2_mount_priv *mp, ushort mode)
{
  int inum = bitmap_alloc(mp, mp->bg.bg_inode_bitmap, mp->sb.s_inodes_count);
  if(inum < 0) return 0;

  // initialise inode
  struct buf *bp = bread(mp->dev, ino_block(mp, inum));
  if(bp == 0){ bitmap_free(mp, mp->bg.bg_inode_bitmap, inum); return 0; }
  struct ext2_inode_disk *ino = (struct ext2_inode_disk*)(bp->data + ino_offset(inum));
  memset(ino, 0, sizeof(struct ext2_inode_disk));
  ino->i_mode = mode;
  ino->i_links_count = 0;
  ino->i_size = 0;

  // B6: set creation timestamp
  ino->i_atime = ticks;
  ino->i_mtime = ticks;
  ino->i_ctime = ticks;

  bwrite(bp);
  brelse(bp);

  acquiresleep(&mp->lock);
  mp->sb.s_free_inodes_count--;
  ext2_sb_write(mp);
  releasesleep(&mp->lock);
  return (uint)inum;
}

static void
ext2_ifree(struct ext2_mount_priv *mp, uint inum)
{
  bitmap_free(mp, mp->bg.bg_inode_bitmap, inum);
  acquiresleep(&mp->lock);
  mp->sb.s_free_inodes_count++;
  ext2_sb_write(mp);
  releasesleep(&mp->lock);
}

// ---------------------------------------------------------------------------
// bmap -- logical block number -> physical block number
// ---------------------------------------------------------------------------
static uint
ext2_bmap(struct ext2_vnode_priv *priv, uint lbn, int alloc)
{
  struct ext2_mount_priv *mp = priv->mp;
  struct ext2_inode_disk *ino = &priv->ino;
  uint *indirect;
  struct buf *bp;

  if(lbn < EXT2_NDIRECT){
    if(ino->i_block[lbn] == 0 && alloc){
      ino->i_block[lbn] = ext2_balloc(mp);
      if(ino->i_block[lbn] == 0) return 0;
      // B7: each new block adds 2 sectors (1024/512) to i_blocks
      ino->i_blocks += 2;
      priv->dirty = 1;
    }
    return ino->i_block[lbn];
  }
  lbn -= EXT2_NDIRECT;

  // single indirect
  if(lbn < EXT2_NINDIRECT){
    uint iblk;
    if(ino->i_block[12] == 0 && alloc){
      iblk = ext2_balloc(mp);
      if(iblk == 0) return 0;
      ino->i_block[12] = iblk;
      ino->i_blocks += 2;   // B7: indirect block itself
      // B4: rollback on bread failure
      bp = bread(mp->dev, iblk);
      if(bp == 0){
        ext2_bfree(mp, iblk);
        ino->i_block[12] = 0;
        ino->i_blocks -= 2;
        return 0;
      }
      memset(bp->data, 0, BSIZE);
      bwrite(bp);
      brelse(bp);
      priv->dirty = 1;
    }
    if(ino->i_block[12] == 0) return 0;
    bp = bread(mp->dev, ino->i_block[12]);
    if(bp == 0) return 0;
    indirect = (uint*)bp->data;
    if(indirect[lbn] == 0 && alloc){
      indirect[lbn] = ext2_balloc(mp);
      if(indirect[lbn]){
        ino->i_blocks += 2;   // B7
        bwrite(bp);
        priv->dirty = 1;
      }
    }
    uint ret = indirect[lbn];
    brelse(bp);
    return ret;
  }
  // double / triple indirect not implemented
  return 0;
}

// ---------------------------------------------------------------------------
// look up name in a directory vnode; return vnode or -1
// ---------------------------------------------------------------------------
static int
ext2_dir_lookup(struct vnode *dir, char *name, struct vnode **result)
{
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)dir->priv;
  struct ext2_inode_disk *ino = &dp->ino;
  uint off = 0;

  while(off < ino->i_size){
    uint blk = ext2_bmap(dp, off / BSIZE, 0);
    if(blk == 0) return -1;
    struct buf *bp = bread(dp->mp->dev, blk);
    if(bp == 0) return -1;

    uint boff = off % BSIZE;
    while(boff < BSIZE){
      struct ext2_dir_entry *de = (struct ext2_dir_entry*)(bp->data + boff);
      if(de->inode != 0 && de->name_len > 0 &&
         strncmp(de->name, name, de->name_len) == 0 &&
         name[de->name_len] == '\0'){
        uint inum = de->inode;
        brelse(bp);
        *result = ext2_iget(dp->mp, inum);
        return (*result == 0) ? -1 : 0;
      }
      if(de->rec_len == 0){ brelse(bp); return -1; }
      boff += de->rec_len;
      off  += de->rec_len;
    }
    brelse(bp);
  }
  return -1;
}

// ---------------------------------------------------------------------------
// add a directory entry to dir (caller must have dir locked already)
// ---------------------------------------------------------------------------
static int
ext2_dir_add(struct vnode *dir, char *name, uint inum, uchar ftype)
{
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)dir->priv;
  struct ext2_inode_disk *ino = &dp->ino;
  uint namelen = strlen(name);
  uint needlen = sizeof(struct ext2_dir_entry) + namelen;
  // align to 4 bytes
  needlen = (needlen + 3) & ~3;

  // Scan for a hole or the last entry (which we can split)
  uint off = 0;
  while(off < ino->i_size){
    uint blk = ext2_bmap(dp, off / BSIZE, 0);
    if(blk == 0) return -1;
    struct buf *bp = bread(dp->mp->dev, blk);
    if(bp == 0) return -1;
    uint boff = off % BSIZE;
    while(boff < BSIZE){
      struct ext2_dir_entry *de = (struct ext2_dir_entry*)(bp->data + boff);
      // B3: guard against corrupt entry with rec_len == 0
      if(de->rec_len == 0){
        brelse(bp);
        return -1;
      }
      uint reclen = de->rec_len;
      uint real_len = sizeof(struct ext2_dir_entry) + de->name_len;
      real_len = (real_len + 3) & ~3;
      if(de->inode == 0 && reclen >= needlen){
        // Found a deleted entry with enough space
        de->inode = inum;
        de->name_len = namelen;
        de->file_type = ftype;
        memmove(de->name, name, namelen);
        bwrite(bp); brelse(bp);
        return 0;
      }
      if(reclen - real_len >= needlen){
        // Split this entry
        de->rec_len = real_len;
        off += real_len;
        boff += real_len;
        struct ext2_dir_entry *nde = (struct ext2_dir_entry*)(bp->data + boff);
        nde->inode = inum;
        nde->rec_len = reclen - real_len;
        nde->name_len = namelen;
        nde->file_type = ftype;
        memmove(nde->name, name, namelen);
        bwrite(bp); brelse(bp);
        return 0;
      }
      boff += reclen;
      off  += reclen;
    }
    brelse(bp);
  }

  // Grow the directory by one block
  uint newblk = ext2_bmap(dp, off / BSIZE, 1);
  if(newblk == 0) return -1;
  struct buf *bp = bread(dp->mp->dev, newblk);
  if(bp == 0) return -1;
  memset(bp->data, 0, BSIZE);
  struct ext2_dir_entry *de = (struct ext2_dir_entry*)bp->data;
  de->inode = inum;
  de->rec_len = BSIZE;
  de->name_len = namelen;
  de->file_type = ftype;
  memmove(de->name, name, namelen);
  ino->i_size = off + BSIZE;
  dp->dirty = 1;
  bwrite(bp); brelse(bp);
  return 0;
}

// ---------------------------------------------------------------------------
// truncate  -- free all data blocks
// ---------------------------------------------------------------------------
static void
ext2_i_truncate(struct ext2_vnode_priv *priv)
{
  struct ext2_inode_disk *ino = &priv->ino;
  uint lbn;

  // Free direct blocks
  for(lbn = 0; lbn < EXT2_NDIRECT && ino->i_block[lbn]; lbn++){
    ext2_bfree(priv->mp, ino->i_block[lbn]);
    ino->i_block[lbn] = 0;
  }

  // Free single indirect
  if(ino->i_block[12]){
    struct buf *bp = bread(priv->mp->dev, ino->i_block[12]);
    if(bp){
      uint *indirect = (uint*)bp->data;
      for(uint i = 0; i < EXT2_NINDIRECT && indirect[i]; i++){
        ext2_bfree(priv->mp, indirect[i]);
        indirect[i] = 0;
      }
      // write back the zeroed indirect block before freeing
      bwrite(bp);
      brelse(bp);
    }
    ext2_bfree(priv->mp, ino->i_block[12]);
    ino->i_block[12] = 0;
  }

  // B7: reset i_blocks to 0 after freeing all blocks
  ino->i_blocks = 0;
  ino->i_size = 0;
  priv->dirty = 1;
}

// ===================================================================
// vnode_ops implementations
// ===================================================================

// forward declarations for functions used before they are defined
static int ext2_unlink(struct vnode *dir, char *name);

static int
ext2_lookup(struct vnode *dir, char *name, struct vnode **result)
{
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)dir->priv;
  // B6: update access time on directory lookup
  ext2_update_time(dp, 1, 0);
  return ext2_dir_lookup(dir, name, result);
}

static int
ext2_read(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct ext2_vnode_priv *priv = (struct ext2_vnode_priv*)vp->priv;
  struct ext2_inode_disk *ino = &priv->ino;
  struct proc *p = myproc();
  int total = 0;

  if(off >= ino->i_size) return 0;
  if(off + n > ino->i_size) n = ino->i_size - off;

  while(total < n){
    uint blk = ext2_bmap(priv, off / BSIZE, 0);
    uint boff = off % BSIZE;
    uint len = BSIZE - boff;
    if(len > n - total) len = n - total;

    if(blk == 0){
      // sparse file hole: fill with zeroes
      for(uint i = 0; i < len; i++){
        if(copyout(p->pagetable, buf + total + i, "\0", 1) < 0)
          return -1;
      }
      total += len;
      off   += len;
      continue;
    }

    struct buf *bp = bread(priv->mp->dev, blk);
    if(bp == 0) return -1;
    if(len > n - total) len = n - total;
    if(copyout(p->pagetable, buf + total, (char *)(bp->data + boff), len) < 0){
      brelse(bp);
      return -1;
    }
    brelse(bp);
    total += len;
    off   += len;
  }
  // B6: update access time on read
  ext2_update_time(priv, 1, 0);
  return total;
}

static int
ext2_write(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct ext2_vnode_priv *priv = (struct ext2_vnode_priv*)vp->priv;
  struct ext2_inode_disk *ino = &priv->ino;
  struct proc *p = myproc();
  int total = 0;

  while(total < n){
    uint blk = ext2_bmap(priv, off / BSIZE, 1);
    if(blk == 0) return -1;
    uint boff = off % BSIZE;
    struct buf *bp = bread(priv->mp->dev, blk);
    if(bp == 0) return -1;
    uint len = BSIZE - boff;
    if(len > n - total) len = n - total;
    if(copyin(p->pagetable, (char *)(bp->data + boff), buf + total, len) < 0){
      brelse(bp);
      return -1;
    }
    bwrite(bp);
    brelse(bp);
    total += len;
    off   += len;
  }
  if(off > ino->i_size){
    ino->i_size = off;
    priv->dirty = 1;
  }
  // B6: update modification time on write
  ext2_update_time(priv, 0, 1);
  return total;
}

static int
ext2_stat(struct vnode *vp, uint64 addr)
{
  struct ext2_vnode_priv *priv = (struct ext2_vnode_priv*)vp->priv;
  struct proc *p = myproc();
  struct stat st;
  struct ext2_inode_disk *ino = &priv->ino;

  st.dev     = vp->dev;
  st.ino     = priv->inum;
  st.type    = vp->type;
  st.nlink   = ino->i_links_count;
  st.size    = ino->i_size;

  if(copyout(p->pagetable, addr, (char*)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

// VFS-level dirent returned by readdir
struct vdirent {
  ushort inum;
  char name[14];
};

static int
ext2_readdir(struct vnode *vp, uint64 buf, uint off)
{
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)vp->priv;
  struct ext2_inode_disk *ino = &dp->ino;
  struct proc *p = myproc();

  while(off < ino->i_size){
    uint blk = ext2_bmap(dp, off / BSIZE, 0);
    if(blk == 0) return -1;
    uint boff = off % BSIZE;
    struct buf *bp = bread(dp->mp->dev, blk);
    if(bp == 0) return -1;

    struct ext2_dir_entry *de = (struct ext2_dir_entry*)(bp->data + boff);
    if(de->rec_len == 0){ brelse(bp); return -1; }
    if(de->inode != 0){
      struct vdirent vde;
      vde.inum = de->inode;
      uint nmlen = de->name_len;
      if(nmlen > 13) nmlen = 13;
      memmove(vde.name, de->name, nmlen);
      vde.name[nmlen] = '\0';
      brelse(bp);
      if(copyout(p->pagetable, buf, (char*)&vde, sizeof(vde)) < 0)
        return -1;
      return off + de->rec_len;
    }
    off += de->rec_len;
    brelse(bp);
  }
  return -1;
}

static int
ext2_create(struct vnode *dir, char *name, short type, struct vnode **new)
{
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)dir->priv;
  struct ext2_mount_priv *mp = dp->mp;
  ushort mode = (type == V_DIR) ? 0x4000 : 0x8000;

  // B6: update modification time on directory change
  ext2_update_time(dp, 0, 1);

  // Check name doesn't exist already
  struct vnode *exist;
  if(ext2_dir_lookup(dir, name, &exist) == 0){
    vput(exist);
    return -1;
  }

  uint inum = ext2_ialloc(mp, mode);
  if(inum == 0) return -1;

  // Initialise inode links count (timestamps already set by ialloc)
  struct ext2_inode_disk ino;
  read_inode(mp, inum, &ino);
  ino.i_links_count = 1;
  write_inode(mp, inum, &ino);

  // Add directory entry
  uchar ftype = (type == V_DIR) ? EXT2_FT_DIR : EXT2_FT_REG;
  if(ext2_dir_add(dir, name, inum, ftype) < 0){
    ext2_ifree(mp, inum);
    return -1;
  }

  if(type == V_DIR){
    // Create . and .. entries
    struct vnode *newdir = ext2_iget(mp, inum);
    if(newdir == 0){
      // rollback: remove the dir entry we just added
      ext2_unlink(dir, name);  // will free the just-allocated inode
      return -1;
    }
    {
      struct ext2_vnode_priv *ndp = (struct ext2_vnode_priv*)newdir->priv;
      if(ext2_dir_add(newdir, ".", inum, EXT2_FT_DIR) < 0 ||
         ext2_dir_add(newdir, "..", dp->inum, EXT2_FT_DIR) < 0){
        vput(newdir);
        ext2_unlink(dir, name);
        return -1;
      }
      ndp->ino.i_links_count = 2;  // . and ..
      ndp->dirty = 1;
      vput(newdir);
    }
    dp->ino.i_links_count++;
    dp->dirty = 1;
  }

  if(new){
    *new = ext2_iget(mp, inum);
    if(*new == 0) return -1;
  }
  return 0;
}

static int
ext2_unlink(struct vnode *dir, char *name)
{
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)dir->priv;
  struct ext2_inode_disk *dino = &dp->ino;
  uint off = 0;

  // B6: update modification time
  ext2_update_time(dp, 0, 1);

  while(off < dino->i_size){
    uint blk = ext2_bmap(dp, off / BSIZE, 0);
    if(blk == 0) return -1;
    struct buf *bp = bread(dp->mp->dev, blk);
    if(bp == 0) return -1;
    uint boff = off % BSIZE;
    while(boff < BSIZE){
      struct ext2_dir_entry *de = (struct ext2_dir_entry*)(bp->data + boff);
      if(de->inode != 0 &&
         strncmp(de->name, name, de->name_len) == 0 &&
         name[de->name_len] == '\0'){
        uint inum = de->inode;
        struct ext2_inode_disk ino;
        read_inode(dp->mp, inum, &ino);
        int is_dir = ((ino.i_mode & 0xF000) == 0x4000);

        if(is_dir){
          // B2: must ensure vdir is valid before proceeding
          struct vnode *vdir = ext2_iget(dp->mp, inum);
          if(vdir == 0){
            // Cannot even read the directory — refuse to unlink
            brelse(bp);
            return -1;
          }
          {
            struct ext2_vnode_priv *vdp = (struct ext2_vnode_priv*)vdir->priv;
            uint doff = 0;
            int empty = 1;
            while(doff < vdp->ino.i_size){
              uint dblk = ext2_bmap(vdp, doff / BSIZE, 0);
              struct buf *dbp = bread(dp->mp->dev, dblk);
              uint dboff = doff % BSIZE;
              struct ext2_dir_entry *dde = (struct ext2_dir_entry*)(dbp->data + dboff);
              int is_dot  = (dde->name_len == 1 && dde->name[0] == '.');
              int is_dotdot = (dde->name_len == 2 && dde->name[0] == '.' && dde->name[1] == '.');
              if(dde->inode != 0 && !is_dot && !is_dotdot){
                empty = 0;
                brelse(dbp);
                break;
              }
              doff += dde->rec_len;
              brelse(dbp);
            }
            vput(vdir);
            if(!empty){ brelse(bp); return -1; }
          }
          dp->dirty = 1;
          dino->i_links_count--;
        }

        // Mark directory entry as unused
        de->inode = 0;
        bwrite(bp);
        brelse(bp);

        // Decrement link count; free if zero
        ino.i_links_count--;
        if(ino.i_links_count == 0){
          struct vnode *victim = ext2_iget(dp->mp, inum);
          if(victim){
            struct ext2_vnode_priv *vp = (struct ext2_vnode_priv*)victim->priv;
            ext2_i_truncate(vp);
            ext2_ifree(dp->mp, inum);
            vput(victim);
          }
        } else {
          // B6: update ctime on link count change
          ino.i_ctime = ticks;
          write_inode(dp->mp, inum, &ino);
        }
        return 0;
      }
      boff += de->rec_len;
      off  += de->rec_len;
    }
    brelse(bp);
  }
  return -1;
}

static int
ext2_mkdir(struct vnode *dir, char *name)
{
  struct vnode *new = 0;
  int r = ext2_create(dir, name, V_DIR, &new);
  if(r == 0 && new) vput(new);
  return r;
}

static int
ext2_link(struct vnode *dir, char *name, struct vnode *old)
{
  struct ext2_vnode_priv *opriv = (struct ext2_vnode_priv*)old->priv;
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)dir->priv;

  if((opriv->ino.i_mode & 0xF000) == 0x4000)
    return -1;  // can't hardlink directories

  // Check target name doesn't exist already
  struct vnode *exist;
  if(ext2_dir_lookup(dir, name, &exist) == 0){
    vput(exist);
    return -1;
  }

  opriv->ino.i_links_count++;
  // B6: update ctime
  opriv->ino.i_ctime = ticks;
  opriv->dirty = 1;

  // B6: update mtime on directory
  ext2_update_time(dp, 0, 1);

  if(ext2_dir_add(dir, name, opriv->inum, EXT2_FT_REG) < 0){
    opriv->ino.i_links_count--;
    opriv->dirty = 1;
    return -1;
  }
  return 0;
}

static int
ext2_mknod(struct vnode *dir, char *name, int major, int minor)
{
  struct ext2_vnode_priv *dp = (struct ext2_vnode_priv*)dir->priv;
  struct ext2_mount_priv *mp = dp->mp;

  // B6: update modification time
  ext2_update_time(dp, 0, 1);

  struct vnode *exist;
  if(ext2_dir_lookup(dir, name, &exist) == 0){ vput(exist); return -1; }

  uint inum = ext2_ialloc(mp, 0x2000);  // char device
  if(inum == 0) return -1;

  struct ext2_inode_disk ino;
  memset(&ino, 0, sizeof(ino));
  ino.i_mode = 0x2000;
  ino.i_links_count = 1;
  // Store major/minor in i_block[0]
  ino.i_block[0] = (major << 8) | (minor & 0xFF);
  write_inode(mp, inum, &ino);

  if(ext2_dir_add(dir, name, inum, EXT2_FT_CHRDEV) < 0){
    ext2_ifree(mp, inum);
    return -1;
  }
  return 0;
}

static int
ext2_read_kernel(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct ext2_vnode_priv *priv = (struct ext2_vnode_priv*)vp->priv;
  struct ext2_inode_disk *ino = &priv->ino;
  int total = 0;

  if(off >= ino->i_size) return 0;
  if(off + n > ino->i_size) n = ino->i_size - off;

  while(total < n){
    uint blk = ext2_bmap(priv, off / BSIZE, 0);
    uint boff = off % BSIZE;
    uint len = BSIZE - boff;
    if(len > n - total) len = n - total;

    if(blk == 0){
      memset((void*)(buf + total), 0, len);
      total += len;
      off   += len;
      continue;
    }

    struct buf *bp = bread(priv->mp->dev, blk);
    if(bp == 0) return -1;
    memmove((void*)(buf + total), bp->data + boff, len);
    brelse(bp);
    total += len;
    off   += len;
  }
  // B6: update access time
  ext2_update_time(priv, 1, 0);
  return total;
}

static int
ext2_truncate(struct vnode *vp)
{
  struct ext2_vnode_priv *priv = (struct ext2_vnode_priv*)vp->priv;
  ext2_i_truncate(priv);
  // B6: update modification time
  ext2_update_time(priv, 0, 1);
  return 0;
}

// ---------------------------------------------------------------------------
// Root vnode (for mount)
// ---------------------------------------------------------------------------
static struct vnode*
ext2_root(struct mount *mp)
{
  return vget(mp->root);
}

// ---------------------------------------------------------------------------
// mount
// ---------------------------------------------------------------------------
struct mount*
ext2_mount(uint dev)
{
  struct mount *mp;
  struct ext2_mount_priv *priv;
  struct buf *bp;

  // Read superblock (block 1 at byte offset 1024)
  bp = bread(dev, 1);
  if(bp == 0) return 0;

  priv = (struct ext2_mount_priv*)kalloc();
  if(priv == 0){ brelse(bp); return 0; }
  memmove(&priv->sb, bp->data, sizeof(struct ext2_superblock));
  brelse(bp);

  if(priv->sb.s_magic != EXT2_MAGIC){
    kfree((void*)priv);
    return 0;
  }

  // Verify block size is 1024
  if(priv->sb.s_log_block_size != 0){
    kfree((void*)priv);
    return 0;  // only 1024-byte blocks supported
  }

  priv->dev = dev;
  initsleeplock(&priv->lock, "ext2_alloc");

  // Read block group descriptor (block 2 when block_size=1024)
  bp = bread(dev, 2);
  if(bp == 0){ kfree((void*)priv); return 0; }
  memmove(&priv->bg, bp->data, sizeof(struct ext2_bgdesc));
  brelse(bp);

  mp = (struct mount*)kalloc();
  if(mp == 0){ kfree((void*)priv); return 0; }
  mp->ops  = &ext2_vfsops;
  mp->dev  = dev;
  mp->priv = priv;
  priv->mnt = mp;  // back-pointer for vnode.mp

  // Get root inode
  mp->root = ext2_iget(priv, EXT2_ROOT_INO);
  if(mp->root == 0){
    kfree((void*)priv);
    kfree((void*)mp);
    return 0;
  }

  return mp;
}

// ---------------------------------------------------------------------------
// ops tables
// ---------------------------------------------------------------------------
static struct vnode_ops ext2_vnops = {
  .lookup      = ext2_lookup,
  .read        = ext2_read,
  .write       = ext2_write,
  .stat        = ext2_stat,
  .readdir     = ext2_readdir,
  .create      = ext2_create,
  .unlink      = ext2_unlink,
  .mkdir       = ext2_mkdir,
  .link        = ext2_link,
  .mknod       = ext2_mknod,
  .read_kernel = ext2_read_kernel,
  .truncate    = ext2_truncate,
  .destroy     = ext2_destroy,
};

static struct vfs_ops ext2_vfsops = {
  .root = ext2_root,
};
