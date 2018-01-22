#ifndef __KERNEL__
#include "stdint.h"
#endif

// # On-disk structures

// The super-block
struct ftfs_super_block {
    int32_t unused1;
    int32_t block_count;
    int32_t unused2[4];
    int32_t log_block_size;
    int32_t unused3;
    int32_t blocks_per_group;
    int32_t unused4[4];
    int16_t unused5[2];
    int16_t magic;
};

// We only have one of those in ftfs, but keep the structure around to stay
// compatible with ext2
struct ftfs_block_group {
    int32_t block_bitmap_block;
    int32_t inode_bitmap_block;
    int32_t inode_table_block;
};

// An inode is 128 bytes. We hardcode this by using sizeof(ftfs_inode).
struct ftfs_inode {
    int16_t mode;
    int16_t uid;
    int32_t size;
    int32_t atime;
    int32_t ctime;
    int32_t mtime;
    int32_t dtime;
    int16_t gid;
    int16_t nlinks;
    int32_t unused2[3];
    int32_t blocks[15]; // 0-11 dir, 12 ind, 13 d-ind, 14 t-ind.
    int32_t unused5[7];
};

struct ftfs_dir {
    int32_t inode;
    int16_t len;
    uint8_t   name_len;
    uint8_t   unused;
    char   name[];
};

