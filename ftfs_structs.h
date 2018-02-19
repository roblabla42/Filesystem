#ifndef FTFS_STRUCTS_H
# define FTFS_STRUCTS_H

# define FORTYTWOFS_MAGIC    0x4242

// The super-block
struct ftfs_super_block {
    __le32 inodes_count;
    __le32 block_count;
    __le32 unused1;
    __le32 free_blocks_count;
    __le32 free_inodes_count;
    __le32 unused2;
    __le32 log_block_size;
    __le32 unused3;
    __le32 blocks_per_group;
    __le32 unused4;
    __le32 inodes_per_group;
    __le32 unused5[2];
    __le16 unused6[2];
    __le16 magic;
};

// We only have one of those in ftfs, but keep the structure around to stay
// compatible with ext2
struct ftfs_block_group {
    __le32 block_bitmap_block;
    __le32 inode_bitmap_block;
    __le32 inode_table_block;
    __le16 free_blocks_count;
    __le16 free_inodes_count;
    __le16 unused[8];
};

// An inode is 128 bytes. We hardcode this by using sizeof(ftfs_inode).
struct ftfs_inode {
    __le16 mode;
    __le16 uid;
    __le32 size;
    __le32 atime;
    __le32 ctime;
    __le32 mtime;
    __le32 dtime;
    __le16 gid;
    __le16 nlinks;
    __le32 block_count;	/* In units of 512 bytes. GENIUS ! -_- */
    __le32 unused2[2];
    __le32 blocks[15]; // 0-11 dir, 12 ind, 13 d-ind, 14 t-ind.
    __le32 unused5[7];
};

struct ftfs_dir {
    __le32 inode;
    __le16 len;
    __u8   name_len;
    __u8   unused;
    char   name[];
};

#endif
