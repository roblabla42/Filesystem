#include "fortytwofs.h"

const struct address_space_operations ft_aops = {
    .readpage       = simple_readpage,
    .write_begin    = simple_write_begin,
    .write_end      = simple_write_end,
};

const struct inode_operations ft_file_inode_operations = {
    .setattr        = simple_setattr, // TODO changer les operations
    .getattr        = simple_getattr,
};

const struct file_operations ft_file_operations = {
    .read_iter    = generic_file_read_iter,
    .write_iter   = generic_file_write_iter,
    .mmap         = generic_file_mmap,
    .fsync        = noop_fsync,
    .splice_read  = generic_file_splice_read,
    .splice_write = iter_file_splice_write,
    .llseek       = generic_file_llseek,
};

struct super_operations ft_ops = {
    .statfs       = simple_statfs,
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

    root_inode = ft_get_inode(sb, NULL, S_IFDIR, FT_ROOT_INODE);
    if (!(sb->s_root = d_make_root(root_inode)))
        return -ENOMEM;
    LOG("got root inode");
    return ret;
}

static struct dentry *ft_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    LOG("Hi");
    return mount_bdev(fs_type, flags, dev_name, data, ft_fill_super);
}

struct file_system_type ft_type = {
    .owner    = THIS_MODULE,
    .name     = "fortytwofs",
    .mount    = ft_mount,
    .kill_sb  = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};

