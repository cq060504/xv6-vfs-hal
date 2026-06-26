// FAT32 filesystem implementation for xv6 VFS (RISC-V only, short 8.3 names).
// Reference: Microsoft Extensible Firmware Initiative FAT32 File System Specification v1.03

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
#include "fat32.h"

static struct vnode_ops fat32_vnops;
static struct vfs_ops  fat32_vfsops;

// ---- per-mount private data ----
struct fat32_mount {
  uint   dev;
  uchar  sec_per_clus;
  uint   rsvd_sec_cnt;
  uchar  num_fats;
  uint   fat_sz;           // sectors per FAT
  uint   first_data_sec;   // first sector of data region
  uint   root_clus;        // root directory cluster
  uint   tot_clus;         // total data clusters
  uint   free_count;       // from FSInfo or scan
  struct sleeplock lock;
};

// ---- per-vnode private data ----
struct fat32_vnode {
  struct fat32_mount *fm;  // owning mount (keeps fm->lock for metadata ops)
  uint  first_clus;        // first cluster of file / directory
  uint  file_size;         // cached size (bytes)
  uchar attrs;             // DIR_Attr copy
  int   dirty;             // 1 if inode info changed
};

// forward declarations
static uint clus_to_sector(struct fat32_mount *fm, uint clus);
static uint read_fat(struct fat32_mount *fm, uint clus);
static int  write_fat(struct fat32_mount *fm, uint clus, uint val);
static uint alloc_clus(struct fat32_mount *fm);
static int  dir_lookup(struct vnode *dir, char *name, struct fat32_dirent *de, uint *off);

// ===================================================================
// helper: cluster <-> sector
// ===================================================================
static uint
clus_to_sector(struct fat32_mount *fm, uint clus)
{
  if(clus < 2)
    panic("fat32: clus_to_sector clus<2");
  return ((clus - 2) * fm->sec_per_clus) + fm->first_data_sec;
}

// ===================================================================
// FAT table read / write
// ===================================================================
static uint
read_fat(struct fat32_mount *fm, uint clus)
{
  uint fat_offset = clus * 4;            // FAT32: 4 bytes per entry
  uint fat_sec    = fm->rsvd_sec_cnt + (fat_offset / BSIZE);
  uint ent_off    = fat_offset % BSIZE;

  struct buf *bp = bread(fm->dev, fat_sec);
  if(bp == 0) panic("fat32: read_fat bread");
  uint val = *(uint*)(bp->data + ent_off) & 0x0FFFFFFF;  // only 28 bits used
  brelse(bp);
  return val;
}

static int
write_fat(struct fat32_mount *fm, uint clus, uint val)
{
  uint fat_offset = clus * 4;
  uint fat_sec    = fm->rsvd_sec_cnt + (fat_offset / BSIZE);
  uint ent_off    = fat_offset % BSIZE;

  struct buf *bp = bread(fm->dev, fat_sec);
  if(bp == 0) return -1;
  uint *entry = (uint*)(bp->data + ent_off);
  *entry = (*entry & 0xF0000000) | (val & 0x0FFFFFFF);  // preserve top 4 bits

  // Write to both FAT copies
  bwrite(bp);
  if(fm->num_fats > 1){
    struct buf *bp2 = bread(fm->dev, fat_sec + fm->fat_sz);
    if(bp2){
      memmove(bp2->data, bp->data, BSIZE);
      bwrite(bp2);
      brelse(bp2);
    }
  }
  brelse(bp);
  return 0;
}

// ===================================================================
// allocate / free clusters
// ===================================================================
static uint
alloc_clus(struct fat32_mount *fm)
{
  uint clus;
  for(clus = 2; clus < fm->tot_clus + 2; clus++){
    if(read_fat(fm, clus) == FAT32_FREE){
      write_fat(fm, clus, FAT32_EOC);
      if(fm->free_count != 0xFFFFFFFF)
        fm->free_count--;
      return clus;
    }
  }
  return 0;  // disk full
}

static void
free_clus_chain(struct fat32_mount *fm, uint clus)
{
  while(clus >= 2 && clus < FAT32_EOC_MIN){
    uint next = read_fat(fm, clus);
    write_fat(fm, clus, FAT32_FREE);
    if(fm->free_count != 0xFFFFFFFF)
      fm->free_count++;
    if(next >= FAT32_EOC_MIN) break;
    clus = next;
  }
}

// ===================================================================
// directory helpers
// ===================================================================

// Convert xv6 filename (lowercase, null-terminated, ≤ 14 chars) to
// FAT 8.3 short name (uppercase, space-padded, 11 bytes).
// Name format: "NAME    EXT" (8+3, dot is implicit).
static void
name_to_83(char *xv6name, uchar fatname[11])
{
  int i, j;

  memset(fatname, ' ', 11);
  // copy main part (up to 8 chars, stop at dot or end)
  for(i = 0; i < 8 && xv6name[i] != '\0' && xv6name[i] != '.'; i++){
    char c = xv6name[i];
    if(c >= 'a' && c <= 'z') c -= 32;  // uppercase
    fatname[i] = c;
  }
  // find dot for extension
  char *dot = 0;
  for(j = 0; xv6name[j]; j++)
    if(xv6name[j] == '.'){ dot = &xv6name[j+1]; break; }
  if(dot){
    for(j = 0; j < 3 && dot[j] != '\0'; j++){
      char c = dot[j];
      if(c >= 'a' && c <= 'z') c -= 32;
      fatname[8 + j] = c;
    }
  }
}

// Compare xv6 name against a FAT 8.3 directory entry.
static int
name_cmp(char *xv6name, uchar fatname[11])
{
  uchar buf[11];
  name_to_83(xv6name, buf);
  return memcmp(buf, fatname, 11);
}

// Find directory entry by name. Returns 0 on success and fills *de + *off.
// *off is the byte offset of this dirent within the directory.
static int
dir_lookup(struct vnode *dir, char *name, struct fat32_dirent *de, uint *off)
{
  struct fat32_vnode *dv = (struct fat32_vnode*)dir->priv;
  struct fat32_mount *fm = dv->fm;
  uint clus = dv->first_clus;
  uint byte_off = 0;

  if((dv->attrs & ATTR_DIRECTORY) == 0)
    return -1;

  while(clus >= 2 && clus < FAT32_EOC_MIN){
    uint sec = clus_to_sector(fm, clus);
    for(uint s = 0; s < fm->sec_per_clus; s++, sec++){
      struct buf *bp = bread(fm->dev, sec);
      if(bp == 0) return -1;
      for(uint b = 0; b < BSIZE; b += 32, byte_off += 32){
        struct fat32_dirent *d = (struct fat32_dirent*)(bp->data + b);
        if(d->DIR_Name[0] == 0x00){  // end of directory
          brelse(bp);
          return -1;
        }
        if(d->DIR_Name[0] == 0xE5)   // deleted entry
          continue;
        if(d->DIR_Attr == ATTR_LONG_NAME)
          continue;
        if(name_cmp(name, d->DIR_Name) == 0){
          memmove(de, d, sizeof(struct fat32_dirent));
          if(off) *off = byte_off;
          brelse(bp);
          return 0;
        }
      }
      brelse(bp);
    }
    clus = read_fat(fm, clus);
  }
  return -1;
}

// Find a free directory entry slot (name[0]==0xE5 or 0x00). Returns offset.
static int
dir_find_free(struct vnode *dir, uint *off)
{
  struct fat32_vnode *dv = (struct fat32_vnode*)dir->priv;
  struct fat32_mount *fm = dv->fm;
  uint clus = dv->first_clus;
  uint byte_off = 0;

  while(clus >= 2 && clus < FAT32_EOC_MIN){
    uint sec = clus_to_sector(fm, clus);
    for(uint s = 0; s < fm->sec_per_clus; s++, sec++){
      struct buf *bp = bread(fm->dev, sec);
      if(bp == 0) return -1;
      for(uint b = 0; b < BSIZE; b += 32, byte_off += 32){
        struct fat32_dirent *d = (struct fat32_dirent*)(bp->data + b);
        if(d->DIR_Name[0] == 0x00 || d->DIR_Name[0] == 0xE5){
          *off = byte_off;
          brelse(bp);
          return 0;
        }
      }
      brelse(bp);
    }
    clus = read_fat(fm, clus);
  }
  return -1;  // directory full
}

// Write a directory entry at byte offset off within directory dir.
static int
dir_write_entry(struct vnode *dir, uint off, struct fat32_dirent *de)
{
  struct fat32_vnode *dv = (struct fat32_vnode*)dir->priv;
  struct fat32_mount *fm = dv->fm;
  uint clus = dv->first_clus;
  uint sectors_per_clus = fm->sec_per_clus;
  uint entry_per_clus = (sectors_per_clus * BSIZE) / 32;

  // Find the right cluster
  uint clus_idx = off / (entry_per_clus * 32);
  for(uint i = 0; i < clus_idx; i++){
    uint next = read_fat(fm, clus);
    if(next >= FAT32_EOC_MIN) return -1;
    clus = next;
  }

  uint boff = off % (sectors_per_clus * BSIZE);
  uint sec = clus_to_sector(fm, clus) + (boff / BSIZE);
  uint ent_off = boff % BSIZE;

  struct buf *bp = bread(fm->dev, sec);
  if(bp == 0) return -1;
  memmove(bp->data + ent_off, de, sizeof(struct fat32_dirent));
  bwrite(bp);
  brelse(bp);
  dv->dirty = 1;
  return 0;
}

// ===================================================================
// vnode alloc / destroy
// ===================================================================
static struct vnode*
fat32_vnode_alloc(struct fat32_mount *fm, uint clus, uint size, uchar attrs)
{
  struct vnode *vp = alloc_vnode();
  struct fat32_vnode *fv;
  if(vp == 0) return 0;

  fv = (struct fat32_vnode*)kalloc();
  if(fv == 0){ vput(vp); return 0; }
  fv->fm = fm;
  fv->first_clus = clus;
  fv->file_size = size;
  fv->attrs = attrs;
  fv->dirty = 0;

  if(attrs & ATTR_DIRECTORY) vp->type = V_DIR;
  else                       vp->type = V_FILE;

  vp->dev   = fm->dev;
  vp->ops   = &fat32_vnops;
  vp->priv  = fv;
  vp->inum  = clus;          // use first cluster as "inode number"
  vp->size  = size;
  return vp;
}

static void
fat32_destroy(void *arg)
{
  struct fat32_vnode *fv = (struct fat32_vnode*)arg;
  if(fv == 0) return;
  // No dirty flag handling needed for basic read support
  kfree((void*)fv);
}

// ===================================================================
// vnode_ops implementations
// ===================================================================
static int
fat32_lookup(struct vnode *dir, char *name, struct vnode **result)
{
  struct fat32_dirent de;
  if(dir_lookup(dir, name, &de, 0) != 0) return -1;

  uint first = ((uint)de.DIR_FstClusHI << 16) | de.DIR_FstClusLO;
  struct fat32_vnode *dv = (struct fat32_vnode*)dir->priv;
  *result = fat32_vnode_alloc(dv->fm, first, de.DIR_FileSize, de.DIR_Attr);
  return (*result == 0) ? -1 : 0;
}

static int
fat32_read(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct fat32_vnode *fv = (struct fat32_vnode*)vp->priv;
  struct fat32_mount *fm = fv->fm;
  struct proc *p = myproc();
  int total = 0;

  if(off >= fv->file_size) return 0;
  if(off + n > fv->file_size) n = fv->file_size - off;
  if(n <= 0) return 0;

  // Walk cluster chain to find starting position
  uint clus = fv->first_clus;
  uint bytes_per_clus = fm->sec_per_clus * BSIZE;
  uint clus_off = off / bytes_per_clus;
  for(uint i = 0; i < clus_off && clus >= 2 && clus < FAT32_EOC_MIN; i++)
    clus = read_fat(fm, clus);
  if(clus < 2 || clus >= FAT32_EOC_MIN) return -1;

  uint inner_off = off % bytes_per_clus;
  while(total < n && clus >= 2 && clus < FAT32_EOC_MIN){
    uint sec = clus_to_sector(fm, clus) + (inner_off / BSIZE);
    uint boff = inner_off % BSIZE;
    uint len = BSIZE - boff;
    if(len > n - total) len = n - total;

    struct buf *bp = bread(fm->dev, sec);
    if(bp == 0) return -1;
    if(copyout(p->pagetable, buf + total, (char*)(bp->data + boff), len) < 0){
      brelse(bp);
      return -1;
    }
    brelse(bp);
    total += len;
    inner_off += len;
    if(inner_off >= bytes_per_clus){
      inner_off = 0;
      clus = read_fat(fm, clus);
    }
  }
  return total;
}

static int
fat32_write(struct vnode *vp, uint64 buf, int n, uint off)
{
  struct fat32_vnode *fv = (struct fat32_vnode*)vp->priv;
  struct fat32_mount *fm = fv->fm;
  struct proc *p = myproc();
  int total = 0;
  uint clus = fv->first_clus;
  uint bytes_per_clus = fm->sec_per_clus * BSIZE;

  // If file has no cluster yet, allocate first one
  if(clus == 0){
    clus = alloc_clus(fm);
    if(clus == 0) return -1;
    fv->first_clus = clus;
    fv->dirty = 1;
  }

  // Walk to starting cluster
  uint clus_off = off / bytes_per_clus;
  for(uint i = 0; i < clus_off; i++){
    uint next = read_fat(fm, clus);
    if(next >= FAT32_EOC_MIN){
      // Need to extend the chain
      uint newc = alloc_clus(fm);
      if(newc == 0) return -1;
      write_fat(fm, clus, newc);
      write_fat(fm, newc, FAT32_EOC);
      clus = newc;
    } else {
      clus = next;
    }
  }

  uint inner_off = off % bytes_per_clus;
  while(total < n){
    if(clus >= FAT32_EOC_MIN || clus < 2){
      // Extend chain
      uint newc = alloc_clus(fm);
      if(newc == 0) break;
      if(clus >= 2) write_fat(fm, clus, newc);
      write_fat(fm, newc, FAT32_EOC);
      clus = newc;
      inner_off = 0;
    }

    uint sec = clus_to_sector(fm, clus) + (inner_off / BSIZE);
    uint boff = inner_off % BSIZE;
    uint len = BSIZE - boff;
    if(len > n - total) len = n - total;

    struct buf *bp = bread(fm->dev, sec);
    if(bp == 0) return -1;
    if(copyin(p->pagetable, (char*)(bp->data + boff), buf + total, len) < 0){
      brelse(bp);
      return -1;
    }
    bwrite(bp);
    brelse(bp);
    total += len;
    inner_off += len;
    if(inner_off >= bytes_per_clus){
      inner_off = 0;
      uint next = read_fat(fm, clus);
      if(next >= FAT32_EOC_MIN && total < n){
        uint newc = alloc_clus(fm);
        if(newc == 0) break;
        write_fat(fm, clus, newc);
        write_fat(fm, newc, FAT32_EOC);
        clus = newc;
      } else {
        clus = next;
      }
    }
  }

  if(off + total > fv->file_size){
    fv->file_size = off + total;
    vp->size = fv->file_size;
    fv->dirty = 1;
  }
  return total;
}

static int
fat32_stat(struct vnode *vp, uint64 addr)
{
  struct fat32_vnode *fv = (struct fat32_vnode*)vp->priv;
  struct proc *p = myproc();
  struct stat st;

  st.dev  = vp->dev;
  st.ino  = fv->first_clus;
  st.type = (fv->attrs & ATTR_DIRECTORY) ? T_DIR : T_FILE;
  st.nlink = 1;
  st.size  = fv->file_size;

  if(copyout(p->pagetable, addr, (char*)&st, sizeof(st)) < 0)
    return -1;
  return 0;
}

static int
fat32_readdir(struct vnode *vp, uint64 buf, uint off)
{
  struct fat32_vnode *fv = (struct fat32_vnode*)vp->priv;
  struct fat32_mount *fm = fv->fm;
  struct proc *p = myproc();
  uint clus = fv->first_clus;

  if((fv->attrs & ATTR_DIRECTORY) == 0) return -1;

  // Seek to position 'off' in the directory
  uint pos = 0;
  while(clus >= 2 && clus < FAT32_EOC_MIN){
    for(uint s = 0; s < fm->sec_per_clus; s++){
      uint sec = clus_to_sector(fm, clus) + s;
      struct buf *bp = bread(fm->dev, sec);
      if(bp == 0) return -1;
      for(uint b = 0; b < BSIZE; b += 32, pos += 32){
        if(pos < off) continue;
        struct fat32_dirent *d = (struct fat32_dirent*)(bp->data + b);
        if(d->DIR_Name[0] == 0x00){ brelse(bp); return -1; }
        if(d->DIR_Name[0] == 0xE5) continue;
        if(d->DIR_Attr == ATTR_LONG_NAME) continue;

        // Build xv6 dirent (inum=first cluster, name=space-trimmed 8.3)
        struct vdirent { ushort inum; char name[14]; } vde;
        vde.inum = ((uint)d->DIR_FstClusHI << 16) | d->DIR_FstClusLO;
        int nl = 0;
        for(int i = 0; i < 8 && d->DIR_Name[i] != ' '; i++)
          vde.name[nl++] = d->DIR_Name[i] >= 'A' && d->DIR_Name[i] <= 'Z'
                           ? d->DIR_Name[i] + 32 : d->DIR_Name[i];
        if(d->DIR_Name[8] != ' '){
          vde.name[nl++] = '.';
          for(int i = 8; i < 11 && d->DIR_Name[i] != ' '; i++)
            vde.name[nl++] = d->DIR_Name[i] >= 'A' && d->DIR_Name[i] <= 'Z'
                             ? d->DIR_Name[i] + 32 : d->DIR_Name[i];
        }
        vde.name[nl] = '\0';
        brelse(bp);
        if(copyout(p->pagetable, buf, (char*)&vde, sizeof(vde)) < 0)
          return -1;
        return pos + 32;
      }
      brelse(bp);
    }
    clus = read_fat(fm, clus);
  }
  return -1;
}

static int
fat32_create(struct vnode *dir, char *name, short type, struct vnode **new)
{
  struct fat32_vnode *dv = (struct fat32_vnode*)dir->priv;
  struct fat32_mount *fm = dv->fm;

  if(dir->type != V_DIR) return -1;

  // Check name doesn't exist
  struct fat32_dirent exist;
  if(dir_lookup(dir, name, &exist, 0) == 0)
    return -1;

  // Allocate first cluster for new file/directory (0 for empty file)
  uint clus = 0;
  if(type == V_DIR){
    clus = alloc_clus(fm);
    if(clus == 0) return -1;
  }

  // Find free directory entry slot
  uint off;
  if(dir_find_free(dir, &off) < 0) return -1;

  // Build directory entry
  struct fat32_dirent de;
  memset(&de, 0, sizeof(de));
  name_to_83(name, de.DIR_Name);
  de.DIR_Attr = (type == V_DIR) ? ATTR_DIRECTORY : ATTR_ARCHIVE;
  de.DIR_FstClusHI = (clus >> 16) & 0xFFFF;
  de.DIR_FstClusLO = clus & 0xFFFF;

  if(dir_write_entry(dir, off, &de) < 0) return -1;

  // For directories: create "." and ".." entries
  if(type == V_DIR){
    uint sec = clus_to_sector(fm, clus);
    struct buf *bp = bread(fm->dev, sec);
    if(bp == 0) return -1;
    memset(bp->data, 0, BSIZE);

    struct fat32_dirent *dot = (struct fat32_dirent*)bp->data;
    memset(dot, ' ', 11);
    dot->DIR_Name[0] = '.';
    dot->DIR_Attr = ATTR_DIRECTORY;
    dot->DIR_FstClusHI = (clus >> 16) & 0xFFFF;
    dot->DIR_FstClusLO = clus & 0xFFFF;

    struct fat32_dirent *dotdot = (struct fat32_dirent*)(bp->data + 32);
    memset(dotdot, ' ', 11);
    dotdot->DIR_Name[0] = '.'; dotdot->DIR_Name[1] = '.';
    dotdot->DIR_Attr = ATTR_DIRECTORY;
    dotdot->DIR_FstClusHI = (dv->first_clus >> 16) & 0xFFFF;
    dotdot->DIR_FstClusLO = dv->first_clus & 0xFFFF;
    bwrite(bp);
    brelse(bp);

    // Mark cluster used in FAT
    write_fat(fm, clus, FAT32_EOC);
  }

  if(new){
    *new = fat32_vnode_alloc(fm, clus, 0, de.DIR_Attr);
    if(*new == 0) return -1;
  }
  return 0;
}

static int
fat32_unlink(struct vnode *dir, char *name)
{
  struct fat32_vnode *dv = (struct fat32_vnode*)dir->priv;
  struct fat32_mount *fm = dv->fm;

  struct fat32_dirent de;
  uint off;
  if(dir_lookup(dir, name, &de, &off) < 0) return -1;

  // Don't unlink non-empty directories
  if(de.DIR_Attr & ATTR_DIRECTORY){
    uint clus = ((uint)de.DIR_FstClusHI << 16) | de.DIR_FstClusLO;
    // Check if empty (only "." and "..")
    uint sec = clus_to_sector(fm, clus);
    struct buf *bp = bread(fm->dev, sec);
    if(bp){
      struct fat32_dirent *entries = (struct fat32_dirent*)bp->data;
      for(uint b = 64; b < BSIZE; b += 32){  // skip . and ..
        if(entries[b/32].DIR_Name[0] != 0x00 && entries[b/32].DIR_Name[0] != 0xE5){
          brelse(bp);
          return -1;  // directory not empty
        }
      }
      brelse(bp);
    }
  }

  // Free cluster chain
  uint clus = ((uint)de.DIR_FstClusHI << 16) | de.DIR_FstClusLO;
  if(clus)
    free_clus_chain(fm, clus);

  // Mark directory entry as deleted
  {
    uint bytes_per_clus = fm->sec_per_clus * BSIZE;
    uint clus_idx = off / (bytes_per_clus);
    uint cluster = dv->first_clus;
    for(uint i = 0; i < clus_idx; i++){
      uint next = read_fat(fm, cluster);
      if(next >= FAT32_EOC_MIN) return -1;
      cluster = next;
    }
    uint boff = off % bytes_per_clus;
    uint del_sec = clus_to_sector(fm, cluster) + (boff / BSIZE);
    uint ent_off = boff % BSIZE;
    struct buf *bp = bread(fm->dev, del_sec);
    if(bp == 0) return -1;
    ((uchar*)bp->data)[ent_off] = 0xE5;  // mark deleted
    bwrite(bp);
    brelse(bp);
  }
  return 0;
}

static int
fat32_mkdir(struct vnode *dir, char *name)
{
  struct vnode *new = 0;
  int r = fat32_create(dir, name, V_DIR, &new);
  if(r == 0 && new) vput(new);
  return r;
}

static int
fat32_truncate(struct vnode *vp)
{
  struct fat32_vnode *fv = (struct fat32_vnode*)vp->priv;
  if(fv->first_clus){
    free_clus_chain(fv->fm, fv->first_clus);
    fv->first_clus = 0;
  }
  fv->file_size = 0;
  vp->size = 0;
  fv->dirty = 1;
  return 0;
}

// ===================================================================
// mount / root / unmount
// ===================================================================
static struct vnode*
fat32_root(struct mount *mp)
{
  return vget(mp->root);
}

static void
fat32_unmount(struct mount *mp)
{
  struct fat32_mount *fm = (struct fat32_mount*)mp->priv;
  if(fm){
    vput(mp->root);
    kfree((void*)fm);
  }
  mp->priv = 0;
  mp->root = 0;
  mp->ops = 0;
}

struct mount*
fat32_mount(uint dev)
{
  // Read boot sector (sector 0)
  struct buf *bp = bread(dev, 0);
  if(bp == 0) return 0;
  struct fat32_bpb *bpb = (struct fat32_bpb*)bp->data;

  // Validate
  if(bpb->BPB_BytsPerSec != BSIZE){ brelse(bp); return 0; }
  if(bpb->BPB_FATSz16 != 0){ brelse(bp); return 0; }       // must be FAT32
  if(bpb->BPB_TotSec16 != 0){ brelse(bp); return 0; }      // must be FAT32
  if(bpb->BPB_FATSz32 == 0){ brelse(bp); return 0; }
  // Check boot signature at offset 510
  if(bp->data[510] != 0x55 || bp->data[511] != 0xAA){ brelse(bp); return 0; }

  struct fat32_mount *fm = (struct fat32_mount*)kalloc();
  if(fm == 0){ brelse(bp); return 0; }
  memset(fm, 0, sizeof(*fm));

  fm->dev           = dev;
  fm->sec_per_clus  = bpb->BPB_SecPerClus;
  fm->rsvd_sec_cnt  = bpb->BPB_RsvdSecCnt;
  fm->num_fats      = bpb->BPB_NumFATs;
  fm->fat_sz        = bpb->BPB_FATSz32;
  fm->root_clus     = bpb->BPB_RootClus;

  // Compute layout
  uint tot_sec = bpb->BPB_TotSec32;
  uint data_sec = tot_sec - fm->rsvd_sec_cnt - (fm->num_fats * fm->fat_sz);
  fm->tot_clus      = data_sec / fm->sec_per_clus;
  fm->first_data_sec = fm->rsvd_sec_cnt + (fm->num_fats * fm->fat_sz);
  fm->free_count    = 0xFFFFFFFF;  // unknown, will scan on demand
  brelse(bp);

  // Read FSInfo for free count
  bp = bread(dev, 1);
  if(bp){
    uint *fsi = (uint*)bp->data;
    if(fsi[0] == FSINFO_LEAD_SIG && fsi[121] == FSINFO_STRUC_SIG)
      fm->free_count = fsi[122];  // FSI_Free_Count at offset 488
    brelse(bp);
  }

  initsleeplock(&fm->lock, "fat32");

  struct mount *mp = (struct mount*)kalloc();
  if(mp == 0){ kfree((void*)fm); return 0; }
  mp->ops  = &fat32_vfsops;
  mp->dev  = dev;
  mp->priv = fm;

  // Create root vnode (cluster = BPB_RootClus, size=0 for directory, attr=DIR)
  mp->root = fat32_vnode_alloc(fm, fm->root_clus, 0, ATTR_DIRECTORY);
  if(mp->root == 0){
    kfree((void*)fm);
    kfree((void*)mp);
    return 0;
  }
  return mp;
}

// ===================================================================
// ops tables
// ===================================================================
static struct vnode_ops fat32_vnops = {
  .lookup      = fat32_lookup,
  .read        = fat32_read,
  .write       = fat32_write,
  .stat        = fat32_stat,
  .readdir     = fat32_readdir,
  .create      = fat32_create,
  .unlink      = fat32_unlink,
  .mkdir       = fat32_mkdir,
  .truncate    = fat32_truncate,
  .destroy     = fat32_destroy,
};

static struct vfs_ops fat32_vfsops = {
  .root    = fat32_root,
  .unmount = fat32_unmount,
};
