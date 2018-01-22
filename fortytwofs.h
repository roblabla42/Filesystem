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

#include "fortytwofs_structs.h"

// # In memory structures

struct ftfs_fs_info {
    // The on-disk super-block description.
    struct ftfs_super_block *super_block;
    struct buffer_head *super_block_bh;
    // For now we only have one group_desc. We'll see about supporting multiple
    // block groups later.
    struct ftfs_block_group **group_desc;
    struct buffer_head **group_desc_bh;
};

struct ftfs_inode_info {
    int blocks[15];
};

extern       struct file_system_type         ft_type;
extern const struct address_space_operations ft_aops;
extern const struct inode_operations         ft_file_inode_operations;
extern const struct file_operations          ft_file_operations;
extern const struct file_operations          ft_dir_file_operations;
