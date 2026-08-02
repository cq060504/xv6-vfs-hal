// FAT32 filesystem for xv6 VFS (RISC-V, 512-byte native sectors).
// Supports both short 8.3 names and VFAT Long File Names (LFN).

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

struct fat32_mount {
  uint dev; uchar sec_per_clus; uint rsvd_sec_cnt;
  uchar num_fats; uint fat_sz; uint first_data_sec;
  uint root_clus; uint tot_clus; uint free_count;
  ushort bps; uint spb; // bytes-per-sector, sectors-per-BSIZE-block
  struct sleeplock lock;
  struct mount *vfs_mount; // back-pointer to VFS mount
};
struct fat32_vnode {
  struct fat32_mount *fm; uint first_clus; uint file_size;
  uchar attrs; int dirty;
  uint pdir_clus;  // first cluster of parent directory (0 for root)
  uint dent_off;   // byte offset of this file's dir entry within parent
};

// ---- sector -> block mapping helpers ----
static int sread(struct fat32_mount *fm, uint sec, uint off, void *buf, uint n){
  uint blk = sec / fm->spb, bo = (sec % fm->spb) * fm->bps + off;
  struct buf *bp = bread(fm->dev, blk);
  if(!bp) return -1;
  memmove(buf, bp->data + bo, n); brelse(bp); return 0;
}
static int swrite(struct fat32_mount *fm, uint sec, uint off, void *buf, uint n){
  uint blk = sec / fm->spb, bo = (sec % fm->spb) * fm->bps + off;
  struct buf *bp = bread(fm->dev, blk);
  if(!bp) return -1;
  memmove(bp->data + bo, buf, n); bwrite(bp);
  if(fm->num_fats>1 && sec>=fm->rsvd_sec_cnt && sec<fm->rsvd_sec_cnt+fm->fat_sz){
    uint s2=sec+fm->fat_sz, blk2=s2/fm->spb, bo2=(s2%fm->spb)*fm->bps+off;
    struct buf *bp2=bread(fm->dev,blk2);
    if(bp2){ memmove(bp2->data+bo2,buf,n); bwrite(bp2); brelse(bp2); }
  }
  brelse(bp); return 0;
}

static uint clus_to_sec(struct fat32_mount *fm, uint c){
  return ((c-2)*fm->sec_per_clus)+fm->first_data_sec;
}
static uint read_fat(struct fat32_mount *fm, uint c){
  uint fo=c*4, sec=fm->rsvd_sec_cnt+fo/fm->bps, eo=fo%fm->bps;
  uint blk=sec/fm->spb, bo=(sec%fm->spb)*fm->bps+eo;
  struct buf *bp=bread(fm->dev,blk);
  if(!bp) panic("fat32:read_fat");
  uint v=*(uint*)(bp->data+bo)&0x0FFFFFFF; brelse(bp); return v;
}
static void write_fat(struct fat32_mount *fm, uint c, uint v){
  uint fo=c*4, sec=fm->rsvd_sec_cnt+fo/fm->bps, eo=fo%fm->bps;
  uint blk=sec/fm->spb, bo=(sec%fm->spb)*fm->bps+eo;
  struct buf *bp=bread(fm->dev,blk); if(!bp) return;
  uint *e=(uint*)(bp->data+bo); *e=(*e&0xF0000000)|(v&0x0FFFFFFF); bwrite(bp);
  if(fm->num_fats>1){
    uint s2=sec+fm->fat_sz, blk2=s2/fm->spb, bo2=(s2%fm->spb)*fm->bps+eo;
    struct buf *bp2=bread(fm->dev,blk2);
    if(bp2){ uint *e2=(uint*)(bp2->data+bo2); *e2=(*e2&0xF0000000)|(v&0x0FFFFFFF); bwrite(bp2); brelse(bp2); }
  }
  brelse(bp);
}
static uint alloc_clus(struct fat32_mount *fm){
  for(uint c=2;c<fm->tot_clus+2;c++) if(read_fat(fm,c)==FAT32_FREE){write_fat(fm,c,FAT32_EOC);return c;}
  return 0;
}
static void free_chain(struct fat32_mount *fm, uint c){
  while(c>=2&&c<FAT32_EOC_MIN){uint n=read_fat(fm,c);write_fat(fm,c,FAT32_FREE);if(n>=FAT32_EOC_MIN)break;c=n;}
}

// ---- 8.3 short name helpers ----
// Convert a user-facing name to 8.3 uppercase short name.
// Handles both "filename" and "filename.ext" forms.
static void name83(char *s, uchar o[11]){
  int i; memset(o,' ',11);
  for(i=0;i<8&&s[i]&&s[i]!='.';i++) o[i]=(s[i]>='a'&&s[i]<='z')?s[i]-32:s[i];
  char *d=0; for(i=0;s[i];i++) if(s[i]=='.'){d=s[i+1]?s+i+1:0;break;}
  if(d) for(i=0;i<3&&d[i];i++) o[8+i]=(d[i]>='a'&&d[i]<='z')?d[i]-32:d[i];
}
static int ncmp(char *s, uchar n[11]){ uchar b[11]; name83(s,b); return memcmp(b,n,11); }

// ASCII case-insensitive string compare (bounded by maxlen).
// STOPS at the first NUL on either side; compares case-insensitively.
static int ncasecmp(char *a, char *b, int maxlen){
  for(int i = 0; i < maxlen; i++){
    char ca = a[i], cb = b[i];
    if(ca >= 'A' && ca <= 'Z') ca += 32;
    if(cb >= 'A' && cb <= 'Z') cb += 32;
    if(ca != cb) return (uchar)ca - (uchar)cb;
    if(ca == 0) return 0;
  }
  return 0;
}

// ---- LFN helpers ----
// Compute the LFN checksum from an 11-byte short name.
static uchar lfn_checksum(uchar *shortname){
  uchar sum = 0;
  for(int i = 0; i < 11; i++)
    sum = (uchar)(((sum & 1) << 7) + (sum >> 1) + shortname[i]);
  return sum;
}

// Number of chars a single LFN entry can carry = 13 Unicode chars.
#define LFN_CHARS_PER_ENTRY  13

// How many LFN directory entries are needed for a given name length.
static int lfn_entries_needed(int namelen){
  if(namelen == 0) return 0;
  return (namelen + LFN_CHARS_PER_ENTRY - 1) / LFN_CHARS_PER_ENTRY;
}

// Check if a name fits in a plain 8.3 short name (no need for LFN).
// A name fits if:
//   - length <= 8 (no extension) with no '.'
//   - or basename <= 8, ext <= 3, exactly one '.'
//   - all characters are valid for 8.3 (essentially ASCII alphanumerics accepted)
// Also: if the short name derived via name83() matches the original (case-insensitively),
// we don't need LFN.
static int fits_83(char *name){
  int len = strlen(name);
  if(len == 0) return 0;
  uchar sn[11];
  name83(name, sn);
  
  // Rebuild the short name as a lowercase string and compare with lowercase input
  char rebuilt[14];
  int rp = 0;
  for(int i = 0; i < 8 && sn[i] != ' '; i++)
    rebuilt[rp++] = sn[i] >= 'A' && sn[i] <= 'Z' ? sn[i] + 32 : sn[i];
  if(sn[8] != ' '){
    rebuilt[rp++] = '.';
    for(int i = 8; i < 11 && sn[i] != ' '; i++)
      rebuilt[rp++] = sn[i] >= 'A' && sn[i] <= 'Z' ? sn[i] + 32 : sn[i];
  }
  rebuilt[rp] = 0;
  
  // Compare lowercase name with rebuilt
  for(int i = 0; i < len; i++){
    char a = name[i];
    if(a >= 'A' && a <= 'Z') a += 32;
    if(a != rebuilt[i]) return 0;
  }
  return rebuilt[rp] == 0;
}

// Generate a unique short name with a numeric tail.
// base6 = first 6 chars of original name (uppercased, stripped of extension and special chars)
// tail_i = numeric suffix 1..999999
static void gen_short_name(char *name, uchar *sn, int tail){
  char tmp[14];
  int tlen = 0;
  // Collect up to 6 valid chars (alphanumeric) from name, skipping dots
  for(int i = 0; name[i] && tlen < 6; i++){
    char c = name[i];
    if(c == '.') continue;
    if(c >= 'a' && c <= 'z') c -= 32;
    if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      tmp[tlen++] = c;
  }
  // Pad with '_' if too short
  while(tlen < 6) tmp[tlen++] = '_';
  tmp[tlen] = 0;
  
  // Format: "TTTTTT~N" where N = tail
  memset(sn, ' ', 11);
  for(int i = 0; i < 6 && i < tlen; i++)
    sn[i] = tmp[i];
  
  // Write "~tail" into the extension area (last 3 bytes)
  int ti = tail;
  char buf[4] = {0};
  if(ti >= 100) ti = 99;  // reasonable cap
  buf[0] = '~';
  if(ti >= 10){
    buf[1] = '0' + (ti / 10);
    buf[2] = '0' + (ti % 10);
  } else {
    buf[1] = '0' + ti;
    buf[2] = ' ';
  }
  // Actually we put "~N" in positions 6,7,8 for simplicity
  // Short name: first 6 chars → positions 0..5, "~tail" → positions 6..8
  sn[6] = buf[0];
  sn[7] = buf[1];
  if(buf[2] != ' ') sn[8] = buf[2];
}

// Check if a specific short name already exists in the directory.
static int shortname_exists(struct vnode *dir, uchar *sn){
  struct fat32_vnode *dv = dir->priv;
  struct fat32_mount *fm = dv->fm;
  uint c = dv->first_clus;
  
  if(!(dv->attrs & ATTR_DIRECTORY)) return 0;
  
  while(c >= 2 && c < FAT32_EOC_MIN){
    for(uint s = 0; s < fm->sec_per_clus; s++){
      uint sec = clus_to_sec(fm, c) + s;
      for(uint b = 0; b < fm->bps; b += 32){
        uchar f;
        if(sread(fm, sec, b, &f, 1) < 0) return 0;
        if(f == 0x00) return 0;   // end of directory
        if(f == 0xE5) continue;   // deleted entry
        struct fat32_dirent d;
        if(sread(fm, sec, b, &d, 32) < 0) return 0;
        if(d.DIR_Attr == ATTR_LONG_NAME) continue;
        if(memcmp(d.DIR_Name, sn, 11) == 0) return 1;
      }
    }
    c = read_fat(fm, c);
  }
  return 0;
}

// ---- Directory lookup with LFN support ----
// On success, fills *de and optionally *off (byte offset from directory start).
// Also fills *lfn_name (if non-NULL) with the reconstructed long name.
// Returns 0 on match, -1 on not found.
static int dlookup(struct vnode *dir, char *nm, struct fat32_dirent *de, uint *off,
                   char *lfn_name){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  uint c=dv->first_clus,pos=0;
  if(!(dv->attrs&ATTR_DIRECTORY)) return -1;
  
  // Temporary buffer to accumulate LFN unicode chars (max 13 * 20 entries = 260)
  ushort lfn_buf[260];
  int lfn_chars = 0;
  uchar lfn_cksum = 0;
  int lfn_active = 0;
  int lfn_next = 0;   // expected seq of the next LFN entry (decrements to 1)
  
  while(c>=2&&c<FAT32_EOC_MIN){
    for(uint s=0;s<fm->sec_per_clus;s++){
      uint sec=clus_to_sec(fm,c)+s;
      for(uint b=0;b<fm->bps;b+=32,pos+=32){
        uchar f; if(sread(fm,sec,b,&f,1)<0) return -1;
        if(f==0x00){
          // End of directory - flush any pending LFN accumulation
          lfn_chars = 0;
          lfn_active = 0;
          return -1;
        }
        if(f==0xE5){
          // Deleted entry - reset LFN accumulation
          lfn_chars = 0;
          lfn_active = 0;
          continue;
        }
        
        struct fat32_dirent d; 
        if(sread(fm,sec,b,&d,32)<0) return -1;
        
        if(d.DIR_Attr == ATTR_LONG_NAME){
          // This is an LFN entry
          struct fat32_lfn *lfn = (struct fat32_lfn*)&d;
          int seq = lfn->seq & ~LFN_LAST;  // 1-based sequence number
          int is_last = (lfn->seq & LFN_LAST) != 0;
          
          if(is_last){
            // Start of a new LFN chain. On disk the entries are written in
            // reverse order: [N|LAST],[N-1],...,[1],<short entry>. The first
            // one we see is seq==N; we then expect N-1, N-2, ... down to 1.
            lfn_active = 1;
            lfn_cksum = lfn->checksum;
            lfn_next = seq - 1;      // expect the next entry to be seq-1
            lfn_chars = seq * LFN_CHARS_PER_ENTRY;
            int base = (seq - 1) * LFN_CHARS_PER_ENTRY;
            for(int i = 0; i < 5 && base + i < 260; i++)
              lfn_buf[base + i] = lfn->name1[i];
            for(int i = 0; i < 6 && base + 5 + i < 260; i++)
              lfn_buf[base + 5 + i] = lfn->name2[i];
            for(int i = 0; i < 2 && base + 11 + i < 260; i++)
              lfn_buf[base + 11 + i] = lfn->name3[i];
            continue;
          }
          
          if(lfn_active && lfn->checksum == lfn_cksum && seq == lfn_next){
            // Accumulate unicode chars from this LFN entry at its proper slot.
            int base = (seq - 1) * LFN_CHARS_PER_ENTRY;
            for(int i = 0; i < 5 && base + i < 260; i++)
              lfn_buf[base + i] = lfn->name1[i];
            for(int i = 0; i < 6 && base + 5 + i < 260; i++)
              lfn_buf[base + 5 + i] = lfn->name2[i];
            for(int i = 0; i < 2 && base + 11 + i < 260; i++)
              lfn_buf[base + 11 + i] = lfn->name3[i];
            lfn_next--;
          } else {
            // Checksum mismatch or sequence error - reset
            lfn_active = 0;
            lfn_chars = 0;
          }
          continue;
        }
        
        // This is a regular directory entry (short name + possibly associated LFN)
        // First, try to match using LFN if we have one accumulated
        if(lfn_active && lfn_chars > 0){
          // Verify checksum
          uchar calc_cksum = lfn_checksum(d.DIR_Name);
          if(calc_cksum == lfn_cksum){
            // Build ASCII string from LFN unicode buffer.
            // Stop at the NUL (0x0000) or the 0xFFFF padding marker.
            char lfn_str[260];
            int lfn_len = 0;
            for(int i = 0; i < lfn_chars && lfn_len < 255; i++){
              if(lfn_buf[i] == 0 || lfn_buf[i] == 0xFFFF) break;
              if(lfn_buf[i] < 128){
                lfn_str[lfn_len++] = (char)lfn_buf[i];
              }
            }
            lfn_str[lfn_len] = 0;
            
            // Compare with requested name (case-insensitive, FAT semantics)
            if(ncasecmp(nm, lfn_str, 256) == 0){
              memmove(de, &d, 32);
              if(off) *off = pos;  // return offset of the short entry (not first LFN)
              if(lfn_name) memmove(lfn_name, lfn_str, lfn_len + 1);
              return 0;
            }
          }
        }
        
        // Also try matching against the short 8.3 name.
        // Special-case "." and "..": their on-disk entries are laid out as
        // "." or ".." followed by spaces, which name83()'s generic 8.3
        // conversion ("        .  ") cannot match (B4 fix).
        if((nm[0]=='.' && nm[1]==0 && d.DIR_Name[0]=='.' && d.DIR_Name[1]==' ') ||
           (nm[0]=='.' && nm[1]=='.' && nm[2]==0 && d.DIR_Name[0]=='.' && d.DIR_Name[1]=='.')){
          memmove(de, &d, 32);
          if(off) *off = pos;
          if(lfn_name) lfn_name[0] = 0;  // no LFN name
          return 0;
        }
        if(ncmp(nm, d.DIR_Name) == 0){
          memmove(de, &d, 32);
          if(off) *off = pos;
          if(lfn_name) lfn_name[0] = 0;  // no LFN name
          return 0;
        }
        
        // No match - reset LFN state and continue
        lfn_chars = 0;
        lfn_active = 0;
      }
    }
    c=read_fat(fm,c);
  }
  return -1;
}

// ---- Find a free directory slot (with enough consecutive entries for LFN) ----
// lfn_cnt = number of LFN entries needed (0 if short name only)
// Returns byte offset of first entry (the first LFN entry, or the short entry if no LFN).
static int dfree(struct vnode *dir, uint *off, int lfn_cnt){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  uint c=dv->first_clus,pos=0;
  int consec = 0;
  uint first_free = 0;
  int total_needed = lfn_cnt + 1;  // LFN entries + short entry
  
  while(c>=2&&c<FAT32_EOC_MIN){
    for(uint s=0;s<fm->sec_per_clus;s++){
      uint sec=clus_to_sec(fm,c)+s;
      for(uint b=0;b<fm->bps;b+=32,pos+=32){
        uchar f; if(sread(fm,sec,b,&f,1)<0) return -1;
        if(f==0x00 || f==0xE5){
          if(consec == 0) first_free = pos;
          consec++;
          if(consec >= total_needed){
            *off = first_free;
            return 0;
          }
        } else {
          // Check if this is a deleted LFN entry (0xE5) — already handled above
          consec = 0;
        }
      }
    }
    c=read_fat(fm,c);
  }
  
  // No gap found in existing clusters; append to end.
  // Walk to the last cluster in the chain.
  c = dv->first_clus;
  if(c == 0){
    // Directory is truly empty (no clusters yet).
    uint newc = alloc_clus(fm);
    if(!newc) return -1;
    dv->first_clus = newc;
    write_fat(fm, newc, FAT32_EOC);
    for(uint s = 0; s < fm->sec_per_clus; s++){
      uchar z[512]; memset(z, 0, 512);
      swrite(fm, clus_to_sec(fm, newc) + s, 0, z, 512);
    }
    *off = 0;
    return 0;
  }
  // Non-empty directory: find the last cluster in the chain.
  while(c >= 2 && c < FAT32_EOC_MIN){
    uint nx = read_fat(fm, c);
    if(nx >= FAT32_EOC_MIN) break;
    c = nx;
  }
  // c is now the last cluster; append a new cluster after it.
  uint newc = alloc_clus(fm);
  if(!newc) return -1;
  write_fat(fm, c, newc);
  write_fat(fm, newc, FAT32_EOC);
  // Zero out the new cluster
  for(uint s = 0; s < fm->sec_per_clus; s++){
    uchar z[512]; memset(z, 0, 512);
    swrite(fm, clus_to_sec(fm, newc) + s, 0, z, 512);
  }
  
  *off = pos;
  dv->dirty = 1;
  return 0;
}

// Write a single 32-byte directory entry at offset off within dir.
static int dwrite_raw(struct vnode *dir, uint off, void *entry, uint len){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  uint epc = (fm->sec_per_clus * fm->bps) / 32;
  uint cluster = dv->first_clus;
  // Walk cluster chain to the target entry
  for(uint i = 0; i < off / (epc * 32); i++){
    uint nx = read_fat(fm, cluster);
    if(nx >= FAT32_EOC_MIN) return -1;
    cluster = nx;
  }
  uint bo = off % (fm->sec_per_clus * fm->bps);
  uint sec = clus_to_sec(fm, cluster) + bo / fm->bps;
  return swrite(fm, sec, bo % fm->bps, entry, len);
}

// Write directory entry at offset (wrapper)
static int dwrite(struct vnode *dir, uint off, struct fat32_dirent *de){
  return dwrite_raw(dir, off, de, 32);
}

// ---- Write back file size / first cluster to the on-disk directory entry ----
// The VFS keeps file size and first cluster in memory for O_APPEND & caching;
// this persists them so a later open() sees the same file. For the root
// directory (pdir_clus == 0) there is no owning entry and nothing to do.
static int sync_dirent(struct vnode *vp){
  struct fat32_vnode *fv = vp->priv;
  if(fv == 0 || fv->pdir_clus == 0) return 0;
  struct fat32_mount *fm = fv->fm;
  uint epc = (fm->sec_per_clus * fm->bps) / 32;
  uint cluster = fv->pdir_clus;
  for(uint i = 0; i < fv->dent_off / (epc * 32); i++){
    uint nx = read_fat(fm, cluster);
    if(nx >= FAT32_EOC_MIN) return -1;
    cluster = nx;
  }
  uint bo = fv->dent_off % (fm->sec_per_clus * fm->bps);
  uint sec = clus_to_sec(fm, cluster) + bo / fm->bps;
  struct fat32_dirent de;
  if(sread(fm, sec, bo % fm->bps, &de, sizeof(de)) < 0) return -1;
  de.DIR_FileSize = fv->file_size;
  de.DIR_FstClusHI = (fv->first_clus >> 16) & 0xFFFF;
  de.DIR_FstClusLO = fv->first_clus & 0xFFFF;
  return swrite(fm, sec, bo % fm->bps, &de, sizeof(de));
}

// ---- Write LFN entries + short entry for a new file ----
// Writes lfn_cnt LFN entries followed by the short entry at offset `*off`.
// Fills in seq, checksum, name1/name2/name3 from `name`.
static int dwrite_lfn(struct vnode *dir, uint off, int lfn_cnt, char *name,
                      struct fat32_dirent *de){
  if(lfn_cnt == 0){
    // Short name only
    return dwrite(dir, off, de);
  }
  
  uchar cksum = lfn_checksum(de->DIR_Name);
  int namelen = strlen(name);
  int total_chars = lfn_cnt * LFN_CHARS_PER_ENTRY;
  
  // Build a 0-terminated unicode buffer padded with 0xFFFF
  ushort uname[260];  // more than enough
  int ui = 0;
  for(int i = 0; i < namelen && ui < total_chars; i++){
    uname[ui++] = (ushort)(uchar)name[i];  // ASCII -> UTF-16LE
  }
  // Pad with 0x0000 for the rest, then trailing 0xFFFF
  uname[ui++] = 0x0000;  // NUL terminator
  while(ui < total_chars) uname[ui++] = 0xFFFF;
  
  // Write LFN entries in reverse order (last entry first on disk)
  for(int ei = lfn_cnt - 1; ei >= 0; ei--){
    struct fat32_lfn lfn;
    memset(&lfn, 0, 32);
    lfn.seq = (uchar)(ei + 1);
    if(ei == lfn_cnt - 1) lfn.seq |= LFN_LAST;
    lfn.attr = ATTR_LONG_NAME;
    lfn.type = 0;
    lfn.checksum = cksum;
    
    int base = ei * LFN_CHARS_PER_ENTRY;
    for(int i = 0; i < 5; i++) lfn.name1[i] = uname[base + i];
    for(int i = 0; i < 6; i++) lfn.name2[i] = uname[base + 5 + i];
    for(int i = 0; i < 2; i++) lfn.name3[i] = uname[base + 11 + i];
    
    if(dwrite_raw(dir, off, &lfn, 32) < 0) return -1;
    off += 32;
  }
  
  // Write the short entry
  return dwrite_raw(dir, off, de, 32);
}

// ---- Count LFN entries already present before a short entry ----
// Given a short entry offset, scan backwards to count preceding LFN entries.
static int count_lfn_backwards(struct vnode *dir, uint short_off){
  struct fat32_vnode *dv = dir->priv;
  struct fat32_mount *fm = dv->fm;
  uchar expected_seq = 1;
  int lfn_cnt = 0;
  
  if(short_off < 32) return 0;
  
  for(uint off = short_off - 32; ; off -= 32){
    struct fat32_dirent d;
    uint epc = (fm->sec_per_clus * fm->bps) / 32;
    uint cluster = dv->first_clus;
    for(uint i = 0; i < off / (epc * 32); i++){
      uint nx = read_fat(fm, cluster);
      if(nx >= FAT32_EOC_MIN) return lfn_cnt;
      cluster = nx;
    }
    uint bo = off % (fm->sec_per_clus * fm->bps);
    uint sec = clus_to_sec(fm, cluster) + bo / fm->bps;
    if(sread(fm, sec, bo % fm->bps, &d, 32) < 0) return lfn_cnt;
    
    if(d.DIR_Attr != ATTR_LONG_NAME) break;
    
    struct fat32_lfn *lfn = (struct fat32_lfn*)&d;
    int seq = lfn->seq & ~LFN_LAST;
    if(seq == expected_seq){
      expected_seq++;
      lfn_cnt++;
    } else {
      break;
    }
    
    if(off == 0) break;
  }
  
  return lfn_cnt;
}

static struct vnode* fvalloc(struct fat32_mount *fm, uint c, uint sz, uchar at){
  struct vnode *vp=alloc_vnode(); if(!vp) return 0;
  struct fat32_vnode *fv=kalloc(); if(!fv){vput(vp);return 0;}
  fv->fm=fm;fv->first_clus=c;fv->file_size=sz;fv->attrs=at;fv->dirty=0;
  fv->pdir_clus=0;   // set by flookup/fcreate when the parent dir entry is known
  fv->dent_off=0;
  vp->type=(at&ATTR_DIRECTORY)?V_DIR:V_FILE;
  vp->mp=fm->vfs_mount;
  vp->dev=fm->dev;vp->ops=&fat32_vnops;vp->priv=fv;vp->inum=c;vp->size=sz;
  return vp;
}
static void fdestroy(void *arg){if(arg)kfree(arg);}

static int flookup(struct vnode *dir, char *nm, struct vnode **res){
  struct fat32_vnode *dv = (struct fat32_vnode*)dir->priv;
  
  // FAT32 root directory has no "." or ".." on-disk entries;
  // for non-root directories ".." is an on-disk entry that carries the
  // parent's first cluster in its FstClus fields.
  if(strncmp(nm, ".", 14) == 0){
    *res = vget(dir);
    return 0;
  }
  if(strncmp(nm, "..", 14) == 0 && dv->first_clus == dv->fm->root_clus){
    *res = vget(dir);  // root's ".." points to itself
    return 0;
  }
  
  struct fat32_dirent de; char lfn[260]; lfn[0] = 0;
  uint off;
  if(dlookup(dir, nm, &de, &off, lfn)) return -1;
  uint c=((uint)de.DIR_FstClusHI<<16)|de.DIR_FstClusLO;
  *res=fvalloc(dv->fm, c, de.DIR_FileSize, de.DIR_Attr);
  if(*res){
    // Remember where this directory entry lives so later file-size /
    // first-cluster updates can be written back to disk (B1 fix).
    struct fat32_vnode *nfv = (*res)->priv;
    nfv->pdir_clus = dv->first_clus;
    nfv->dent_off = off;
  }
  return *res?0:-1;
}
static int fread(struct vnode *vp, uint64 buf, int n, uint off){
  struct fat32_vnode *fv=vp->priv; struct fat32_mount *fm=fv->fm;
  if(off>=fv->file_size) return 0;
  if(off+n>fv->file_size) n=fv->file_size-off;
  uint c=fv->first_clus, bpc=fm->sec_per_clus*fm->bps, cskip=off/bpc, total=0;
  for(uint i=0;i<cskip&&c>=2&&c<FAT32_EOC_MIN;i++) c=read_fat(fm,c);
  uint in=off%bpc;
  while(total<n&&c>=2&&c<FAT32_EOC_MIN){
    uint sec=clus_to_sec(fm,c)+in/fm->bps, bo=in%fm->bps, len=fm->bps-bo;
    if(len>n-total) len=n-total;
    uchar tmp[512];
    if(sread(fm,sec,bo,tmp,len)<0) return -1;
    if(copyout(myproc()->pagetable,buf+total,(char*)tmp,len)<0) return -1;
    total+=len; in+=len;
    if(in>=bpc){in=0;c=read_fat(fm,c);}
  }
  return total;
}
static int fwrite(struct vnode *vp, uint64 buf, int n, uint off){
  struct fat32_vnode *fv=vp->priv; struct fat32_mount *fm=fv->fm;
  uint c=fv->first_clus, bpc=fm->sec_per_clus*fm->bps, total=0;
  if(!c){c=alloc_clus(fm);if(!c)return -1;fv->first_clus=c;}
  uint cskip=off/bpc;
  for(uint i=0;i<cskip;i++){
    uint nx=read_fat(fm,c); if(nx>=FAT32_EOC_MIN){nx=alloc_clus(fm);if(!nx)return -1;write_fat(fm,c,nx);write_fat(fm,nx,FAT32_EOC);}
    c=nx;
  }
  uint in=off%bpc;
  while(total<n){
    if(c>=FAT32_EOC_MIN){c=alloc_clus(fm);if(!c)break;in=0;}
    uint sec=clus_to_sec(fm,c)+in/fm->bps, bo=in%fm->bps, len=fm->bps-bo;
    if(len>n-total) len=n-total;
    uchar tmp[512];
    if(copyin(myproc()->pagetable,(char*)tmp,buf+total,len)<0) return -1;
    if(swrite(fm,sec,bo,tmp,len)<0) return -1;
    total+=len; in+=len;
    if(in>=bpc){in=0;uint nx=read_fat(fm,c);if(nx>=FAT32_EOC_MIN&&total<n){nx=alloc_clus(fm);if(!nx)break;write_fat(fm,c,nx);write_fat(fm,nx,FAT32_EOC);c=nx;}else c=nx;}
  }
  if(off+total>fv->file_size){fv->file_size=off+total;vp->size=fv->file_size;}
  // Persist size and first cluster so a later open() on this path sees the
  // same file (B1 fix).  Directory entries (attr 0x10) never hold a size and
  // must not be touched here.
  if(!(fv->attrs & ATTR_DIRECTORY))
    sync_dirent(vp);
  return total;
}
static int fstat(struct vnode *vp, uint64 addr){
  struct fat32_vnode *fv=vp->priv; struct stat st;
  st.dev=vp->dev;st.ino=fv->first_clus;
  st.type=(fv->attrs&ATTR_DIRECTORY)?T_DIR:T_FILE;st.nlink=1;st.size=fv->file_size;
  return copyout(myproc()->pagetable,addr,(char*)&st,sizeof(st))<0?-1:0;
}

// ---- readdir with LFN support ----
static int freaddir(struct vnode *vp, uint64 buf, uint off){
  struct fat32_vnode *fv=vp->priv; struct fat32_mount *fm=fv->fm;
  if(!(fv->attrs&ATTR_DIRECTORY)) return -1;
  uint c=fv->first_clus,pos=0;
  
  // Skip to off
  while(c>=2&&c<FAT32_EOC_MIN){
    for(uint s=0;s<fm->sec_per_clus;s++){
      uint sec=clus_to_sec(fm,c)+s;
      for(uint b=0;b<fm->bps;b+=32,pos+=32){
        if(pos < off) continue;
        
        // At this point, pos >= off. We need to find the next valid entry.
        uchar f; 
        if(sread(fm,sec,b,&f,1)<0) return -1;
        if(f==0x00) return -1;  // end of directory
        if(f==0xE5){ continue; } // skip deleted
        
        struct fat32_dirent d;
        if(sread(fm,sec,b,&d,32)<0) return -1;
        
        // If this is an LFN entry, accumulate it and continue
        if(d.DIR_Attr == ATTR_LONG_NAME){
          continue;
        }
        
        // This is a regular entry. Reconstruct the full name from preceding LFN entries.
        struct {ushort inum; char name[14];} vde;
        vde.inum = ((uint)d.DIR_FstClusHI<<16) | d.DIR_FstClusLO;
        
        // Try to read backwards to find LFN entries
        int lfn_count = 0;
        ushort lfn_buf[260];
        int total_lfn_chars = 0;
        
        // Scan backwards from pos to find LFN entries associated with this short entry
        uint back_off = pos;
        int expected_seq = 1;
        uchar expected_cksum = lfn_checksum(d.DIR_Name);
        
        while(back_off >= 32){
          back_off -= 32;
          struct fat32_dirent prev;
          
          // Read the previous entry
          uint bc = fv->first_clus;
          uint bpos = back_off;
          uint epc = (fm->sec_per_clus * fm->bps) / 32;
          for(uint i = 0; i < bpos / (epc * 32); i++){
            uint nx = read_fat(fm, bc);
            if(nx >= FAT32_EOC_MIN) break;
            bc = nx;
          }
          uint bo = bpos % (fm->sec_per_clus * fm->bps);
          uint bsec = clus_to_sec(fm, bc) + bo / fm->bps;
          
          uchar pf;
          if(sread(fm, bsec, bo % fm->bps, &pf, 1) < 0) break;
          if(pf == 0x00 || pf == 0xE5) break;  // end of chain
          if(sread(fm, bsec, bo % fm->bps, &prev, 32) < 0) break;
          
          if(prev.DIR_Attr != ATTR_LONG_NAME) break;
          
          struct fat32_lfn *lfn = (struct fat32_lfn*)&prev;
          int seq = lfn->seq & ~LFN_LAST;
          if(lfn->checksum != expected_cksum) break;
          if(seq != expected_seq) break;
          
          expected_seq++;
          lfn_count++;
          
          // Accumulate chars in reverse order (they'll be reconstructed later)
          // Store them temporarily
          int start_idx = (seq - 1) * LFN_CHARS_PER_ENTRY;
          if(start_idx + 5 <= 260){
            for(int i = 0; i < 5 && start_idx + i < 260; i++) lfn_buf[start_idx + i] = lfn->name1[i];
          }
          if(start_idx + 11 <= 260){
            for(int i = 0; i < 6 && start_idx + 5 + i < 260; i++) lfn_buf[start_idx + 5 + i] = lfn->name2[i];
          }
          if(start_idx + 13 <= 260){
            for(int i = 0; i < 2 && start_idx + 11 + i < 260; i++) lfn_buf[start_idx + 11 + i] = lfn->name3[i];
          }
          total_lfn_chars = start_idx + 13;
        }
        
        if(lfn_count > 0 && total_lfn_chars > 0){
          // Build ASCII name from LFN buffer.
          // ABI: struct dirent.name can hold at most 13 chars + NUL.
          // If the LFN fits, emit it verbatim (it round-trips through
          // dlookup, which matches LFNs case-insensitively).
          // If it is longer than 13 chars, fall back to the 8.3 DOS name
          // in the directory entry: that name is unique and dlookup's
          // short-name match finds it exactly, so `ls` output stays usable.
          int nl = 0;
          for(int i = 0; i < total_lfn_chars && nl < 13; i++){
            if(lfn_buf[i] == 0) break;  // NUL terminator
            if(lfn_buf[i] == 0xFFFF) break;  // padding
            if(lfn_buf[i] < 128)
              vde.name[nl++] = (char)lfn_buf[i];
          }
          // Check whether a non-padding char was cut off by the 13-char limit;
          // if so the truncated name would not match dlookup (lookup uses the
          // full LFN), so present the DOS short name instead.
          int cut_off = 0;
          for(int i = nl; i < total_lfn_chars; i++){
            if(lfn_buf[i] != 0 && lfn_buf[i] != 0xFFFF && lfn_buf[i] < 128){
              cut_off = 1;
              break;
            }
          }
          if(cut_off){
            nl = 0;
            for(int i = 0; i < 8 && d.DIR_Name[i] != ' '; i++)
              vde.name[nl++] = (d.DIR_Name[i] >= 'A' && d.DIR_Name[i] <= 'Z') ? d.DIR_Name[i] + 32 : d.DIR_Name[i];
            if(d.DIR_Name[8] != ' '){
              vde.name[nl++] = '.';
              for(int i = 8; i < 11 && d.DIR_Name[i] != ' '; i++)
                vde.name[nl++] = (d.DIR_Name[i] >= 'A' && d.DIR_Name[i] <= 'Z') ? d.DIR_Name[i] + 32 : d.DIR_Name[i];
            }
            vde.name[nl] = 0;
          } else {
            vde.name[nl] = 0;
          }
        } else {
          // Use short 8.3 name
          int nl = 0;
          for(int i=0;i<8&&d.DIR_Name[i]!=' ';i++) vde.name[nl++]=(d.DIR_Name[i]>='A'&&d.DIR_Name[i]<='Z')?d.DIR_Name[i]+32:d.DIR_Name[i];
          if(d.DIR_Name[8]!=' '){vde.name[nl++]='.';for(int i=8;i<11&&d.DIR_Name[i]!=' ';i++) vde.name[nl++]=(d.DIR_Name[i]>='A'&&d.DIR_Name[i]<='Z')?d.DIR_Name[i]+32:d.DIR_Name[i];}
          vde.name[nl]=0;
        }
        
        if(copyout(myproc()->pagetable,buf,(char*)&vde,sizeof(vde))<0) return -1;
        return pos + 32;
      }
    }
    c=read_fat(fm,c);
  }
  return -1;
}

// ---- create with LFN support ----
static int fcreate(struct vnode *dir, char *nm, short type, struct vnode **new){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  if(dir->type!=V_DIR) return -1;
  
  struct fat32_dirent ex; char dummy[260];
  if(dlookup(dir, nm, &ex, 0, dummy)==0) return -1;
  
  uint c=0; if(type==V_DIR){c=alloc_clus(fm);if(!c)return -1;}
  
  // Determine if we need LFN entries
  int lfn_cnt = 0;
  if(!fits_83(nm)){
    lfn_cnt = lfn_entries_needed(strlen(nm));
  }
  
  uint off;
  if(dfree(dir, &off, lfn_cnt) < 0) return -1;
  
  // Build short directory entry
  struct fat32_dirent de; memset(&de, 0, 32);
  if(lfn_cnt == 0){
    name83(nm, de.DIR_Name);
  } else {
    // Generate a unique short name
    for(int tail = 1; tail <= 99; tail++){
      gen_short_name(nm, de.DIR_Name, tail);
      if(!shortname_exists(dir, de.DIR_Name)) break;
    }
  }
  de.DIR_Attr = (type==V_DIR) ? ATTR_DIRECTORY : ATTR_ARCHIVE;
  de.DIR_FstClusHI = (c>>16) & 0xFFFF;
  de.DIR_FstClusLO = c & 0xFFFF;
  
  // Write LFN entries + short entry
  if(dwrite_lfn(dir, off, lfn_cnt, nm, &de) < 0) return -1;
  
  if(type==V_DIR){
    uint sec=clus_to_sec(fm,c); uchar z[512]; memset(z,0,512);
    struct fat32_dirent *dot=(struct fat32_dirent*)z,*dotdot=(struct fat32_dirent*)(z+32);
    memset(dot->DIR_Name,' ',11);dot->DIR_Name[0]='.';dot->DIR_Attr=ATTR_DIRECTORY;
    dot->DIR_FstClusHI=(c>>16)&0xFFFF;dot->DIR_FstClusLO=c&0xFFFF;
    memset(dotdot->DIR_Name,' ',11);dotdot->DIR_Name[0]='.';dotdot->DIR_Name[1]='.';dotdot->DIR_Attr=ATTR_DIRECTORY;
    dotdot->DIR_FstClusHI=(dv->first_clus>>16)&0xFFFF;dotdot->DIR_FstClusLO=dv->first_clus&0xFFFF;
    swrite(fm,sec,0,z,512);write_fat(fm,c,FAT32_EOC);
  }
  if(new){
    *new=fvalloc(fm,c,0,de.DIR_Attr);
    if(!*new) return -1;
    // Remember the on-disk directory entry location so size/first-cluster
    // changes made through this vnode can be written back (B1 fix).
    // NOTE: dfree() returns the offset of the FIRST free slot, which holds
    // the first LFN entry when lfn_cnt>0; the short entry (which stores the
    // file size / first cluster) lives lfn_cnt*32 bytes later.
    // Flookup's dlookup() already returns the short-entry offset directly.
    struct fat32_vnode *nfv = (*new)->priv;
    nfv->pdir_clus = dv->first_clus;
    nfv->dent_off = off + lfn_cnt * 32;
  }
  return 0;
}

// ---- unlink with LFN support ----
static int funlink(struct vnode *dir, char *nm){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  struct fat32_dirent de; uint off; char lfn[260]; lfn[0] = 0;
  if(dlookup(dir, nm, &de, &off, lfn) < 0) return -1;
  
  if(de.DIR_Attr&ATTR_DIRECTORY){
    uint c=((uint)de.DIR_FstClusHI<<16)|de.DIR_FstClusLO;
    for(uint b=64;b<fm->bps;b+=32){
      uchar f; if(sread(fm,clus_to_sec(fm,c),b,&f,1)<0) return -1;
      if(f!=0x00&&f!=0xE5) return -1;
    }
  }
  
  uint c=((uint)de.DIR_FstClusHI<<16)|de.DIR_FstClusLO; if(c) free_chain(fm,c);
  
  // If there are preceding LFN entries, mark them all as deleted too
  int preceding_lfn = count_lfn_backwards(dir, off);
  uint first_off = off - preceding_lfn * 32;
  uint epc = (fm->sec_per_clus * fm->bps) / 32;
  
  // Mark all entries (LFN + short) as deleted
  for(int i = 0; i <= preceding_lfn; i++){
    uint this_off = first_off + i * 32;
    uint ci2 = this_off / (epc * 32);
    uint clust2 = dv->first_clus;
    for(uint j = 0; j < ci2; j++){
      clust2 = read_fat(fm, clust2);
      if(clust2 >= FAT32_EOC_MIN) return -1;
    }
    uint bo2 = this_off % (fm->sec_per_clus * fm->bps);
    uint dsec2 = clus_to_sec(fm, clust2) + bo2 / fm->bps;
    uchar e5 = 0xE5;
    swrite(fm, dsec2, bo2 % fm->bps, &e5, 1);
  }
  
  return 0;
}

static int fmkdir(struct vnode *dir, char *nm){
  struct vnode *n=0; int r=fcreate(dir,nm,V_DIR,&n); if(!r&&n) vput(n); return r;
}
static int ftrunc(struct vnode *vp){
  struct fat32_vnode *fv=vp->priv;
  if(fv->first_clus){free_chain(fv->fm,fv->first_clus);fv->first_clus=0;}
  fv->file_size=0;vp->size=0;
  if(!(fv->attrs & ATTR_DIRECTORY))
    sync_dirent(vp);
  return 0;
}

static struct vnode* froot(struct mount *mp){return vget(mp->root);}
static void fumount(struct mount *mp){
  if(mp->priv){vput(mp->root);kfree(mp->priv);}
  mp->priv=0;mp->root=0;mp->ops=0;
}
struct mount* fat32_mount(uint dev){
  struct buf *bp=bread(dev,0); if(!bp) return 0;
  struct fat32_bpb bpb; memmove(&bpb,bp->data,sizeof(bpb));
  ushort bps=bpb.BPB_BytsPerSec;
  if(bps!=512||bpb.BPB_FATSz32==0){brelse(bp);return 0;}
  if(bp->data[510]!=0x55||bp->data[511]!=0xAA){brelse(bp);return 0;}
  brelse(bp);
  struct fat32_mount *fm=kalloc(); if(!fm) return 0; memset(fm,0,sizeof(*fm));
  fm->dev=dev; fm->bps=bps; fm->spb=BSIZE/bps;
  fm->sec_per_clus=bpb.BPB_SecPerClus;fm->rsvd_sec_cnt=bpb.BPB_RsvdSecCnt;
  fm->num_fats=bpb.BPB_NumFATs;fm->fat_sz=bpb.BPB_FATSz32;fm->root_clus=bpb.BPB_RootClus;
  uint ts=bpb.BPB_TotSec16?bpb.BPB_TotSec16:bpb.BPB_TotSec32;
  uint ds=ts-fm->rsvd_sec_cnt-(fm->num_fats*fm->fat_sz);
  fm->tot_clus=ds/fm->sec_per_clus; fm->first_data_sec=fm->rsvd_sec_cnt+(fm->num_fats*fm->fat_sz);
  fm->free_count=0xFFFFFFFF; initsleeplock(&fm->lock,"fat32");
  struct mount *mp=kalloc(); if(!mp){kfree(fm);return 0;}
  mp->ops=&fat32_vfsops;mp->dev=dev;mp->priv=fm;
  fm->vfs_mount = mp;
  mp->root=fvalloc(fm,fm->root_clus,0,ATTR_DIRECTORY);
  if(!mp->root){kfree(fm);kfree(mp);return 0;}
  return mp;
}

static struct vnode_ops fat32_vnops={
  .lookup=flookup,.read=fread,.write=fwrite,.stat=fstat,.readdir=freaddir,
  .create=fcreate,.unlink=funlink,.mkdir=fmkdir,.truncate=ftrunc,.destroy=fdestroy,
};
static struct vfs_ops fat32_vfsops={.root=froot,.unmount=fumount};
