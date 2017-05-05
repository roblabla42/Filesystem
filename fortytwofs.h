#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/time.h>

#define LOG(x) printk(KERN_DEBUG "%s: %s\n", __FUNCTION__, x)

#define FT_ROOT_INODE    1
#define FORTYTWOFS_MAGIC 0x4242


struct inode *ft_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, ino_t ino);

extern       struct file_system_type         ft_type;
extern const struct address_space_operations ft_aops;
extern const struct inode_operations         ft_file_inode_operations;
extern const struct file_operations          ft_file_operations;

