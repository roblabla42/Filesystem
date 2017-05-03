#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/time.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ntibi");
MODULE_DESCRIPTION("testelele");

#define LOG(x) printk(KERN_DEBUG "%s: %s\n", __FUNCTION__, x)

#define FT_ROOT_INODE    1
#define FORTYTWOFS_MAGIC 0x4242

static const struct address_space_operations ft_aops = {
    .readpage       = simple_readpage,
    .write_begin    = simple_write_begin,
    .write_end      = simple_write_end,
};

static const struct inode_operations ft_file_inode_operations = {
    .setattr        = simple_setattr, // TODO changer les operations
    .getattr        = simple_getattr,
};

static int ft_fops_set(void *data, u64 val) { LOG("Hi"); return 0; }
static int ft_fops_get(void *data, u64 *val) { LOG("Hi"); return 0; }
DEFINE_SIMPLE_ATTRIBUTE(ft_file_operations, ft_fops_get, ft_fops_set, "%llx\n");

static struct inode *ft_get_inode(struct super_block *sb, unsigned long ino, umode_t mode)
{
    struct inode *inode;

    LOG("Hi");
    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;

    LOG("new inode");
    inode_init_owner(inode, NULL, mode); /* TODO: dir=NULL, il faut peut etre mettre un truc */
    inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
    switch (inode->i_mode & S_IFMT)
    {
    case S_IFREG:           LOG("reg");
        inode->i_op             = &ft_file_inode_operations;
        inode->i_mapping->a_ops = &ft_aops;
        inode->i_fop            = &ft_file_operations;
        break;
    case S_IFDIR:           LOG("dir");
        inode->i_op             = &simple_dir_inode_operations;
        inode->i_mapping->a_ops = &ft_aops;
        inode->i_fop            = &simple_dir_operations;
        break;
    case S_IFLNK:           LOG("link");
        inode->i_op             = &simple_symlink_inode_operations;
        break;
    default:                LOG("special");
        init_special_inode(inode, inode->i_mode, 0);
        break;
    }

    unlock_new_inode(inode);
    return inode;
}

static const struct super_operations ft_ops = { // TODO: meilleures op: http://lxr.free-electrons.com/source/fs/ext2/super.c#L323
    .statfs       = simple_statfs,
    .drop_inode   = generic_delete_inode,
    .show_options = generic_show_options,
};

static int ft_fill_super(struct super_block *sb, void *data, int silent)
{ // TODO: recuperer des meilleurs infos: http://lxr.free-electrons.com/source/fs/ext2/super.c#L785
    struct inode *root_inode;
    int ret = 0;

    LOG("Hi");
    sb->s_maxbytes       = MAX_LFS_FILESIZE;
    sb->s_blocksize      = BLOCK_SIZE;
    sb->s_blocksize_bits = BLOCK_SIZE_BITS;
    sb->s_magic          = FORTYTWOFS_MAGIC;
    sb->s_op             = &ft_ops;
    sb->s_time_gran      = 1;

    root_inode = ft_get_inode(sb, FT_ROOT_INODE, S_IFDIR); // TODO: il faut que cette inode soit S_IFDIR
    if (!(sb->s_root = d_make_root(root_inode)))
        return -ENOMEM;
    LOG("root inode created");
    return ret;
}

static struct dentry *ft_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    LOG("Hi");
    return mount_bdev(fs_type, flags, dev_name, data, ft_fill_super);
}

static struct file_system_type ft_type = {
    .owner    = THIS_MODULE,
    .name     = "fortytwofs",
    .mount    = ft_mount, // TODO tout se passe bien au retour de ft_mount,
    .kill_sb  = kill_block_super, // va falloir chercher ou sa plante
    .fs_flags = FS_REQUIRES_DEV,
};

static int __init ft_init(void)
{
    int ret = 0;

    LOG("Hi");
    ret = register_filesystem(&ft_type);
    return ret;
}

static void __exit ft_cleanup(void)
{
    LOG("Hi");
    unregister_filesystem(&ft_type);
    return ;
}

module_init(ft_init);
module_exit(ft_cleanup);
