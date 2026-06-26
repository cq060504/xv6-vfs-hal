// FAT32 filesystem for xv6 VFS (RISC-V, short 8.3 names, 512-byte native sectors).

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
};
struct fat32_vnode {
  struct fat32_mount *fm; uint first_clus; uint file_size;
  uchar attrs; int dirty;
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

static void name83(char *s, uchar o[11]){
  int i; memset(o,' ',11);
  for(i=0;i<8&&s[i]&&s[i]!='.';i++) o[i]=(s[i]>='a'&&s[i]<='z')?s[i]-32:s[i];
  char *d=0; for(i=0;s[i];i++) if(s[i]=='.'){d=s+i+1;break;}
  if(d) for(i=0;i<3&&d[i];i++) o[8+i]=(d[i]>='a'&&d[i]<='z')?d[i]-32:d[i];
}
static int ncmp(char *s, uchar n[11]){ uchar b[11]; name83(s,b); return memcmp(b,n,11); }

static int dlookup(struct vnode *dir, char *nm, struct fat32_dirent *de, uint *off){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  uint c=dv->first_clus,pos=0;
  if(!(dv->attrs&ATTR_DIRECTORY)) return -1;
  while(c>=2&&c<FAT32_EOC_MIN){
    for(uint s=0;s<fm->sec_per_clus;s++){
      uint sec=clus_to_sec(fm,c)+s;
      for(uint b=0;b<fm->bps;b+=32,pos+=32){
        struct fat32_dirent d;
        if(sread(fm,sec,b,&d,32)<0) return -1;
        if(d.DIR_Name[0]==0x00) return -1;
        if(d.DIR_Name[0]==0xE5||d.DIR_Attr==ATTR_LONG_NAME) continue;
        if(ncmp(nm,d.DIR_Name)==0){memmove(de,&d,32);if(off)*off=pos;return 0;}
      }
    }
    c=read_fat(fm,c);
  }
  return -1;
}
static int dfree(struct vnode *dir, uint *off){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  uint c=dv->first_clus,pos=0;
  while(c>=2&&c<FAT32_EOC_MIN){
    for(uint s=0;s<fm->sec_per_clus;s++){
      uint sec=clus_to_sec(fm,c)+s;
      for(uint b=0;b<fm->bps;b+=32,pos+=32){
        uchar f; if(sread(fm,sec,b,&f,1)<0) return -1;
        if(f==0x00||f==0xE5){*off=pos;return 0;}
      }
    }
    c=read_fat(fm,c);
  }
  return -1;
}
static int dwrite(struct vnode *dir, uint off, struct fat32_dirent *de){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  uint c=dv->first_clus, epc=(fm->sec_per_clus*fm->bps)/32;
  for(uint i=0;i<off/(epc*32);i++){c=read_fat(fm,c);if(c>=FAT32_EOC_MIN)return -1;}
  uint bo=off%(fm->sec_per_clus*fm->bps), sec=clus_to_sec(fm,c)+bo/fm->bps;
  return swrite(fm,sec,bo%fm->bps,de,32);
}

static struct vnode* fvalloc(struct fat32_mount *fm, uint c, uint sz, uchar at){
  struct vnode *vp=alloc_vnode(); if(!vp) return 0;
  struct fat32_vnode *fv=kalloc(); if(!fv){vput(vp);return 0;}
  fv->fm=fm;fv->first_clus=c;fv->file_size=sz;fv->attrs=at;fv->dirty=0;
  vp->type=(at&ATTR_DIRECTORY)?V_DIR:V_FILE;
  vp->dev=fm->dev;vp->ops=&fat32_vnops;vp->priv=fv;vp->inum=c;vp->size=sz;
  return vp;
}
static void fdestroy(void *arg){if(arg)kfree(arg);}

static int flookup(struct vnode *dir, char *nm, struct vnode **res){
  struct fat32_dirent de;
  if(dlookup(dir,nm,&de,0)) return -1;
  uint c=((uint)de.DIR_FstClusHI<<16)|de.DIR_FstClusLO;
  *res=fvalloc(((struct fat32_vnode*)dir->priv)->fm,c,de.DIR_FileSize,de.DIR_Attr);
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
  return total;
}
static int fstat(struct vnode *vp, uint64 addr){
  struct fat32_vnode *fv=vp->priv; struct stat st;
  st.dev=vp->dev;st.ino=fv->first_clus;
  st.type=(fv->attrs&ATTR_DIRECTORY)?T_DIR:T_FILE;st.nlink=1;st.size=fv->file_size;
  return copyout(myproc()->pagetable,addr,(char*)&st,sizeof(st))<0?-1:0;
}
static int freaddir(struct vnode *vp, uint64 buf, uint off){
  struct fat32_vnode *fv=vp->priv; struct fat32_mount *fm=fv->fm;
  if(!(fv->attrs&ATTR_DIRECTORY)) return -1;
  uint c=fv->first_clus,pos=0;
  while(c>=2&&c<FAT32_EOC_MIN){
    for(uint s=0;s<fm->sec_per_clus;s++){
      uint sec=clus_to_sec(fm,c)+s;
      for(uint b=0;b<fm->bps;b+=32,pos+=32){
        if(pos<off) continue;
        struct fat32_dirent d; if(sread(fm,sec,b,&d,32)<0) return -1;
        if(d.DIR_Name[0]==0x00) return -1;
        if(d.DIR_Name[0]==0xE5||d.DIR_Attr==ATTR_LONG_NAME) continue;
        struct {ushort inum; char name[14];} vde;
        vde.inum=((uint)d.DIR_FstClusHI<<16)|d.DIR_FstClusLO;
        int nl=0;
        for(int i=0;i<8&&d.DIR_Name[i]!=' ';i++) vde.name[nl++]=(d.DIR_Name[i]>='A'&&d.DIR_Name[i]<='Z')?d.DIR_Name[i]+32:d.DIR_Name[i];
        if(d.DIR_Name[8]!=' '){vde.name[nl++]='.';for(int i=8;i<11&&d.DIR_Name[i]!=' ';i++) vde.name[nl++]=(d.DIR_Name[i]>='A'&&d.DIR_Name[i]<='Z')?d.DIR_Name[i]+32:d.DIR_Name[i];}
        vde.name[nl]=0;
        if(copyout(myproc()->pagetable,buf,(char*)&vde,sizeof(vde))<0) return -1;
        return pos+32;
      }
    }
    c=read_fat(fm,c);
  }
  return -1;
}
static int fcreate(struct vnode *dir, char *nm, short type, struct vnode **new){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  if(dir->type!=V_DIR) return -1;
  struct fat32_dirent ex; if(dlookup(dir,nm,&ex,0)==0) return -1;
  uint c=0; if(type==V_DIR){c=alloc_clus(fm);if(!c)return -1;}
  uint off; if(dfree(dir,&off)<0) return -1;
  struct fat32_dirent de; memset(&de,0,32);
  name83(nm,de.DIR_Name); de.DIR_Attr=(type==V_DIR)?ATTR_DIRECTORY:ATTR_ARCHIVE;
  de.DIR_FstClusHI=(c>>16)&0xFFFF;de.DIR_FstClusLO=c&0xFFFF;
  if(dwrite(dir,off,&de)<0) return -1;
  if(type==V_DIR){
    uint sec=clus_to_sec(fm,c); uchar z[512]; memset(z,0,512);
    struct fat32_dirent *dot=(struct fat32_dirent*)z,*dotdot=(struct fat32_dirent*)(z+32);
    memset(dot->DIR_Name,' ',11);dot->DIR_Name[0]='.';dot->DIR_Attr=ATTR_DIRECTORY;
    dot->DIR_FstClusHI=(c>>16)&0xFFFF;dot->DIR_FstClusLO=c&0xFFFF;
    memset(dotdot->DIR_Name,' ',11);dotdot->DIR_Name[0]='.';dotdot->DIR_Name[1]='.';dotdot->DIR_Attr=ATTR_DIRECTORY;
    dotdot->DIR_FstClusHI=(dv->first_clus>>16)&0xFFFF;dotdot->DIR_FstClusLO=dv->first_clus&0xFFFF;
    swrite(fm,sec,0,z,512);write_fat(fm,c,FAT32_EOC);
  }
  if(new){*new=fvalloc(fm,c,0,de.DIR_Attr);if(!*new)return -1;}
  return 0;
}
static int funlink(struct vnode *dir, char *nm){
  struct fat32_vnode *dv=dir->priv; struct fat32_mount *fm=dv->fm;
  struct fat32_dirent de; uint off;
  if(dlookup(dir,nm,&de,&off)<0) return -1;
  if(de.DIR_Attr&ATTR_DIRECTORY){
    uint c=((uint)de.DIR_FstClusHI<<16)|de.DIR_FstClusLO;
    for(uint b=64;b<fm->bps;b+=32){
      uchar f; if(sread(fm,clus_to_sec(fm,c),b,&f,1)<0) return -1;
      if(f!=0x00&&f!=0xE5) return -1;
    }
  }
  uint c=((uint)de.DIR_FstClusHI<<16)|de.DIR_FstClusLO; if(c) free_chain(fm,c);
  uint epc=(fm->sec_per_clus*fm->bps)/32, ci=off/(epc*32), cluster=dv->first_clus;
  for(uint i=0;i<ci;i++){cluster=read_fat(fm,cluster);if(cluster>=FAT32_EOC_MIN)return -1;}
  uint bo=off%(fm->sec_per_clus*fm->bps), dsec=clus_to_sec(fm,cluster)+bo/fm->bps;
  uchar e5=0xE5; swrite(fm,dsec,bo%fm->bps,&e5,1);
  return 0;
}
static int fmkdir(struct vnode *dir, char *nm){
  struct vnode *n=0; int r=fcreate(dir,nm,V_DIR,&n); if(!r&&n) vput(n); return r;
}
static int ftrunc(struct vnode *vp){
  struct fat32_vnode *fv=vp->priv;
  if(fv->first_clus){free_chain(fv->fm,fv->first_clus);fv->first_clus=0;}
  fv->file_size=0;vp->size=0;return 0;
}

static struct vnode* froot(struct mount *mp){return vget(mp->root);}
static void fumount(struct mount *mp){
  if(mp->priv){vput(mp->root);kfree(mp->priv);}
  mp->priv=0;mp->root=0;mp->ops=0;
}
struct mount* fat32_mount(uint dev){
  struct buf *bp=bread(dev,0); if(!bp) return 0;
  struct fat32_bpb *bpb=(struct fat32_bpb*)bp->data;
  ushort bps=bpb->BPB_BytsPerSec;
  if(bps!=512||bpb->BPB_FATSz32==0){brelse(bp);return 0;}
  if(bp->data[510]!=0x55||bp->data[511]!=0xAA){brelse(bp);return 0;}
  brelse(bp);
  struct fat32_mount *fm=kalloc(); if(!fm) return 0; memset(fm,0,sizeof(*fm));
  fm->dev=dev; fm->bps=bps; fm->spb=BSIZE/bps;
  fm->sec_per_clus=bpb->BPB_SecPerClus;fm->rsvd_sec_cnt=bpb->BPB_RsvdSecCnt;
  fm->num_fats=bpb->BPB_NumFATs;fm->fat_sz=bpb->BPB_FATSz32;fm->root_clus=bpb->BPB_RootClus;
  uint ts=bpb->BPB_TotSec16?bpb->BPB_TotSec16:bpb->BPB_TotSec32;
  uint ds=ts-fm->rsvd_sec_cnt-(fm->num_fats*fm->fat_sz);
  fm->tot_clus=ds/fm->sec_per_clus; fm->first_data_sec=fm->rsvd_sec_cnt+(fm->num_fats*fm->fat_sz);
  fm->free_count=0xFFFFFFFF; initsleeplock(&fm->lock,"fat32");
  struct mount *mp=kalloc(); if(!mp){kfree(fm);return 0;}
  mp->ops=&fat32_vfsops;mp->dev=dev;mp->priv=fm;
  mp->root=fvalloc(fm,fm->root_clus,0,ATTR_DIRECTORY);
  if(!mp->root){kfree(fm);kfree(mp);return 0;}
  return mp;
}

static struct vnode_ops fat32_vnops={
  .lookup=flookup,.read=fread,.write=fwrite,.stat=fstat,.readdir=freaddir,
  .create=fcreate,.unlink=funlink,.mkdir=fmkdir,.truncate=ftrunc,.destroy=fdestroy,
};
static struct vfs_ops fat32_vfsops={.root=froot,.unmount=fumount};
