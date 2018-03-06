#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/time.h>

#define LOG(x, ...) printk(KERN_DEBUG "%s: " x "\n", __FUNCTION__, ##__VA_ARGS__)

// We cannot define FT_ROOT_INODE to 0, because it signifies the absence of an
// inode, similar to NULL.
#define FT_ROOT_INODE       2

int ft_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create);

/* inode.c */
struct inode *ft_new_inode(struct inode *dir, umode_t mode, dev_t rdev);
struct inode *ft_get_inode(struct super_block *sb, ino_t ino);
int ft_write_inode(struct inode *inode, struct writeback_control *wbc);
int ftfs_finish_inode_creation(struct inode *inode, struct inode *dir,
					struct dentry *dentry);

struct ftfs_dir;
typedef int (*ft_iterator)(struct ftfs_dir*, void*, int*);

int ft_iterate(struct inode *inode, ft_iterator it, loff_t *pos, void *data);
int ft_insert_inode_in_dir(struct inode *dir, const char *name, ino_t ino);
struct page *ft_get_page(struct inode *inode, int nbr);
void ft_put_page(struct page *page);
void ft_evict_inode(struct inode *inode);

/* dir.c */
int ft_is_dir_not_empty(struct inode *inode);
int ft_update_dotdot(struct inode *dir, unsigned long long ino);

/* symlink.c */
int	ft_init_symlink_inode(struct inode *inode);
int	ft_symlink(struct inode *dir, struct dentry *dentry,
		const char *symname);

// # On-disk structures

# include "ftfs_structs.h"

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

/*
 * The i_block of the inode, stored in the inode->i_private field.
 */
struct ftfs_inode_info {
    int blocks[15];
};

/*
 * Retrieves the ftfs_inode_info of an inode, saved in the inode->i_private
 * @inode: the inode to get it from
 */
#define ft_get_inode_info(inode)	\
	((struct ftfs_inode_info *)inode->i_private)

static inline long ft_get_max_filename_len(struct super_block *sb)
{
	/* The maxfilename is a direntry taking the whole block */
	return (sb->s_blocksize - sizeof(struct ftfs_dir));
}

extern       struct file_system_type         ft_type;
extern const struct address_space_operations ft_aops;
extern const struct inode_operations         ft_file_inode_operations;
extern const struct inode_operations         ft_special_inode_operations;
extern const struct file_operations          ft_file_operations;
extern const struct file_operations          ft_dir_file_operations;
