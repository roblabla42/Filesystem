#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/time.h>

#define LOG(x, ...) printk(KERN_DEBUG "%s: " x "\n", __FUNCTION__, ##__VA_ARGS__)

// We cannot define FT_ROOT_INODE to 0, because it signifies the absence of an
// inode, similar to NULL.
#define FT_ROOT_INODE    2
#define FORTYTWOFS_MAGIC 0x4242


int ft_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create);
struct inode *ft_get_inode(struct super_block *sb, ino_t ino);
int ft_write_inode(struct inode *inode, struct writeback_control *wbc);

struct ftfs_dir;
typedef int (*ft_iterator)(struct ftfs_dir*, void*);

int ft_iterate(struct inode *inode, ft_iterator it, loff_t *pos, void *data);
int ft_insert_inode_in_dir(struct inode *dir, struct dentry *dentry, ino_t ino);

// # On-disk structures

// The super-block
struct ftfs_super_block {
    __le32 unused1[6];
    __le32 log_block_size;
    __le32 unused2[6];
    __le16 unused3[2];
    __le16 magic;
};

// We only have one of those in ftfs, but keep the structure around to stay
// compatible with ext2
struct ftfs_block_group {
    __le32 block_bitmap_block;
    __le32 inode_bitmap_block;
    __le32 inode_table_block;
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
    __le32 unused2[3];
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

// # In memory structures

struct ftfs_fs_info {
    // The on-disk super-block description.
    struct ftfs_super_block *super_block;
    struct buffer_head *super_block_bh;
    // For now we only have one group_desc. We'll see about supporting multiple
    // block groups later.
    struct ftfs_block_group *group_desc;
    struct buffer_head *group_desc_bh;
};

struct ftfs_inode_info {
    int blocks[15];
};

extern       struct file_system_type         ft_type;
extern const struct address_space_operations ft_aops;
extern const struct inode_operations         ft_file_inode_operations;
extern const struct file_operations          ft_file_operations;
extern const struct file_operations          ft_dir_file_operations;
