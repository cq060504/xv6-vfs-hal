// FAT32 on-disk structures (little-endian, short 8.3 names only).
// Reference: Microsoft Extensible Firmware Initiative FAT32 File System Specification v1.03

// ---- BPB (BIOS Parameter Block) — boot sector offsets ----
struct fat32_bpb {
  uchar  BS_jmpBoot[3];       // 0x00: jump instruction (EB xx 90 or E9 xx xx)
  uchar  BS_OEMName[8];       // 0x03: OEM name, often "MSWIN4.1"
  ushort BPB_BytsPerSec;      // 0x0B: bytes per sector, usually 512
  uchar  BPB_SecPerClus;      // 0x0D: sectors per cluster (power of 2)
  ushort BPB_RsvdSecCnt;      // 0x0E: reserved sector count (typical 32 for FAT32)
  uchar  BPB_NumFATs;         // 0x10: number of FATs (usually 2)
  ushort BPB_RootEntCnt;      // 0x11: 0 for FAT32
  ushort BPB_TotSec16;        // 0x13: 0 for FAT32
  uchar  BPB_Media;           // 0x15: media descriptor (0xF8 for fixed disk)
  ushort BPB_FATSz16;         // 0x16: 0 for FAT32
  ushort BPB_SecPerTrk;       // 0x18
  ushort BPB_NumHeads;        // 0x1A
  uint   BPB_HiddSec;         // 0x1C: hidden sectors before partition
  uint   BPB_TotSec32;        // 0x20: total sectors in volume

  // FAT32 extended fields (offset 36)
  uint   BPB_FATSz32;         // 0x24: sectors per FAT
  ushort BPB_ExtFlags;        // 0x28
  ushort BPB_FSVer;           // 0x2A
  uint   BPB_RootClus;        // 0x2C: root directory cluster (usually 2)
  ushort BPB_FSInfo;          // 0x30: FSInfo sector (usually 1)
  ushort BPB_BkBootSec;       // 0x32: backup boot sector (usually 6)
  uchar  BPB_Reserved[12];    // 0x34
  uchar  BS_DrvNum;           // 0x40
  uchar  BS_Reserved1;        // 0x41
  uchar  BS_BootSig;          // 0x42: 0x29 if next 3 fields valid
  uint   BS_VolID;            // 0x43: volume serial number
  uchar  BS_VolLab[11];       // 0x47: volume label
  uchar  BS_FilSysType[8];    // 0x52: "FAT32   "
  // Boot code + 0xAA55 signature follow...
} __attribute__((packed));

// ---- FAT32 directory entry (short name, 32 bytes) ----
struct fat32_dirent {
  uchar  DIR_Name[11];        // 0x00: short name (8.3, space-padded)
  uchar  DIR_Attr;            // 0x0B: attributes
  uchar  DIR_NTRes;           // 0x0C: reserved for NT
  uchar  DIR_CrtTimeTenth;    // 0x0D: creation time (tenths of second)
  ushort DIR_CrtTime;         // 0x0E: creation time
  ushort DIR_CrtDate;         // 0x10: creation date
  ushort DIR_LastAccDate;     // 0x12: last access date
  ushort DIR_FstClusHI;       // 0x14: high word of first cluster
  ushort DIR_WrtTime;         // 0x16: last write time
  ushort DIR_WrtDate;         // 0x18: last write date
  ushort DIR_FstClusLO;       // 0x1A: low word of first cluster
  uint   DIR_FileSize;        // 0x1C: file size in bytes
} __attribute__((packed));

// Directory attribute bits
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20
#define ATTR_LONG_NAME  (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

// ---- FAT32 cluster values ----
#define FAT32_EOC_MIN   0x0FFFFFF8   // minimum EOC (end of cluster chain)
#define FAT32_EOC       0x0FFFFFFF   // standard EOC
#define FAT32_BAD       0x0FFFFFF7   // bad cluster
#define FAT32_FREE      0x00000000   // free cluster
#define FAT32_RESERVED  0x0FFFFFF0   // reserved cluster range start

// First data sector = BPB_RsvdSecCnt + (BPB_NumFATs * BPB_FATSz32)
// FirstSectorOfCluster(N) = ((N - 2) * BPB_SecPerClus) + FirstDataSector

// ---- FSInfo sector offsets ----
#define FSINFO_LEAD_SIG   0x41615252
#define FSINFO_STRUC_SIG  0x61417272
#define FSINFO_TRAIL_SIG  0xAA550000
