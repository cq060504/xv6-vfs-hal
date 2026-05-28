// ext2 on-disk structures.
// Block size is assumed to be 1024 bytes (matches xv6 BSIZE).

// Superblock: at byte offset 1024 (block 1 when block_size=1024)
struct ext2_superblock {
  uint s_inodes_count;
  uint s_blocks_count;
  uint s_r_blocks_count;
  uint s_free_blocks_count;
  uint s_free_inodes_count;
  uint s_first_data_block;    // 1 if block_size=1024, 0 otherwise
  uint s_log_block_size;      // 0 => 1024 bytes
  uint s_log_frag_size;
  uint s_blocks_per_group;
  uint s_frags_per_group;
  uint s_inodes_per_group;
  uint s_mtime;
  uint s_wtime;
  ushort s_mnt_count;
  ushort s_max_mnt_count;
  ushort s_magic;             // 0xEF53
  ushort s_state;
  ushort s_errors;
  ushort s_minor_rev_level;
  uint s_lastcheck;
  uint s_checkinterval;
  uint s_creator_os;
  uint s_rev_level;
  ushort s_def_resuid;
  ushort s_def_resgid;
  // Extended superblock fields (rev_level >= 1)
  uint s_first_ino;
  ushort s_inode_size;        // 128
  ushort s_block_group_nr;
  uint s_feature_compat;
  uint s_feature_incompat;
  uint s_feature_ro_compat;
  uchar s_uuid[16];
  char s_volume_name[16];
  char s_last_mounted[64];
  uint s_algo_bitmap;
  uchar s_prealloc_blocks;
  uchar s_prealloc_dir_blocks;
  ushort __pad1;
  uchar s_journal_uuid[16];
  uint s_journal_inum;
  uint s_journal_dev;
  uint s_last_orphan;
  uint s_hash_seed[4];
  uchar s_def_hash_version;
  uchar __pad2[3];
  uint s_default_mount_options;
  uint s_first_meta_bg;
  uchar __pad3[760];
};

#define EXT2_MAGIC 0xEF53

// Block group descriptor (32 bytes)
struct ext2_bgdesc {
  uint bg_block_bitmap;
  uint bg_inode_bitmap;
  uint bg_inode_table;
  ushort bg_free_blocks_count;
  ushort bg_free_inodes_count;
  ushort bg_used_dirs_count;
  ushort bg_pad;
  uchar bg_reserved[12];
};

// Inode types
#define EXT2_INODE_FIFO   1
#define EXT2_INODE_CHRDEV 2
#define EXT2_INODE_DIR    4
#define EXT2_INODE_BLKDEV 6
#define EXT2_INODE_REG    8
#define EXT2_INODE_SYMLNK 10
#define EXT2_INODE_SOCK   12

// On-disk inode (128 bytes)
struct ext2_inode_disk {
  ushort i_mode;
  ushort i_uid;
  uint i_size;
  uint i_atime;
  uint i_ctime;
  uint i_mtime;
  uint i_dtime;
  ushort i_gid;
  ushort i_links_count;
  uint i_blocks;    // count of 512-byte sectors
  uint i_flags;
  uint i_osd1;
  uint i_block[15]; // 0-11: direct, 12: single indirect, 13: double, 14: triple
  uint i_generation;
  uint i_file_acl;
  uint i_dir_acl;
  uint i_faddr;
  uchar i_osd2[12];
};

// Directory entry
struct ext2_dir_entry {
  uint inode;
  ushort rec_len;
  uchar name_len;
  uchar file_type;
  char name[];  // variable length, padded to rec_len
};

#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG      1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLNK   7

#define EXT2_ROOT_INO    2

#define EXT2_NDIRECT     12
#define EXT2_NINDIRECT   (BSIZE / sizeof(uint))
#define EXT2_DINDIRECT   (EXT2_NINDIRECT * EXT2_NINDIRECT)
#define EXT2_MAXFILESIZE (EXT2_NDIRECT * BSIZE + \
                          EXT2_NINDIRECT * BSIZE + \
                          EXT2_DINDIRECT * BSIZE + \
                          EXT2_DINDIRECT * EXT2_NINDIRECT * BSIZE)
