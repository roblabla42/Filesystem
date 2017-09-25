#include "fortytwofs.h"
#include <linux/buffer_head.h>
#include <uapi/linux/magic.h>
#include <linux/slab.h>
#include <linux/mpage.h>
static int ft_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create)
{
    // A block is 1024 bytes, right ?
    struct ftfs_inode *ft_inode = (struct ftfs_inode*)inode->i_private;
    map_bh(bh, inode->i_sb, ft_inode->blocks[iblock]);
    return 0;
}

static int ft_readpage(struct file *file, struct page *page)
{
    return mpage_readpage(page, ft_get_block);
}

// This is going to be where the actual reading/writing happens. The kernel will
// call those functions whenever it needs to pull data from the fs to the
// pagecache.
const struct address_space_operations ft_aops = {
    .readpage       = ft_readpage,
    /*.write_begin    = simple_write_begin,
    .write_end      = simple_write_end,*/
};

// TODO changer les operations
// roblabla: Actually, I don't think we need to. It turns out we're supposed
// to write the inode in the `super_block->write_inode` callback, and not
// directly in setattr. And since we're not going to support all the additional
// features of ext2, the simple_getattr/setattr should do the trick just fine.
const struct inode_operations ft_file_inode_operations = {
    .setattr        = simple_setattr,
    .getattr        = simple_getattr,
};

const struct file_operations ft_file_operations = {
    .read_iter    = generic_file_read_iter,
    .write_iter   = generic_file_write_iter,
    .mmap         = generic_file_mmap,
    .fsync        = generic_file_fsync,
    .splice_read  = generic_file_splice_read,
    .splice_write = iter_file_splice_write,
    .llseek       = generic_file_llseek,
};

extern const struct super_operations ft_ops;

static int ft_fill_super(struct super_block *sb, void *data, int silent)
{ // TODO: recuperer des meilleurs infos: http://lxr.free-electrons.com/source/fs/ext2/super.c#L785
    struct inode *root_inode;
    int ret = 0;
    struct buffer_head *bh;
    struct ftfs_fs_info *fsinfo;
    int blocksize;

    ret = -ENOMEM;

    // Not too sure what the default sb blocksize is (seems to be 4096, but when
    // I tried to use it without initializing sb_min_blocksize, it acted weird).
    // The sb blocksize is basically undefined. We set it to 1024 since most of
    // the data on ext2 and ftfs have at most that size (the sb is 1024 bytes)
    blocksize = sb_min_blocksize(sb, 1024);
    if (!blocksize)
        goto failed;

    if ((fsinfo = kzalloc(sizeof(struct ftfs_fs_info), GFP_KERNEL)) == NULL)
        goto failed;

    // By default, the first superblock is on the second block (first being
    // reserved for the bootloader). It's a bit of a weird requirement given
    // partitions shouldn't overlap with the bootloader at all. I'd like to
    // get rid of this eventually. But let's stay ext2-compatible for now.
    if ((bh = sb_bread(sb, 1024 / blocksize)) == NULL)
        goto failed_fsinfo;
    fsinfo->super_block = (struct ftfs_super_block*)bh->b_data + (1024 % blocksize);
    fsinfo->super_block_bh = bh;

    // Get the block group info. The block group
    if ((bh = sb_bread(sb, 2048 / blocksize)) == NULL)
        goto failed_super_block;
    fsinfo->group_desc = (struct ftfs_block_group*)bh->b_data + (2048 % blocksize);
    fsinfo->group_desc_bh = bh;

    sb->s_fs_info = fsinfo;
    sb->s_magic = fsinfo->super_block->magic;
    // In the name of ext2 compat, check for EXT2_SUPER_MAGIC too !
    if (sb->s_magic != EXT2_SUPER_MAGIC && sb->s_magic != FORTYTWOFS_MAGIC)
        goto cantfind_ftfs;

    // TODO: Calculate max file size.
    sb->s_maxbytes       = MAX_LFS_FILESIZE;
    sb->s_op             = &ft_ops;
    sb->s_time_gran      = 1;

    root_inode = ft_get_inode(sb, FT_ROOT_INODE);
    if (!(sb->s_root = d_make_root(root_inode)))
        goto failed_release;

    return 0;
cantfind_ftfs:
    LOG("error: can't find an ft filesystem on device %s, got %lx, blocksize=%ld.", sb->s_id, sb->s_magic, sb->s_blocksize);
failed_super_block:
    brelse(fsinfo->group_desc_bh);
failed_release:
    brelse(fsinfo->super_block_bh);
failed_fsinfo: 
    kfree(fsinfo);
failed:
    sb->s_fs_info = NULL;
    return ret;
}

static void ft_put_super(struct super_block *sb)
{
    struct ftfs_fs_info *fsinfo = (struct ftfs_fs_info*)sb->s_fs_info;

    LOG("Releasing superblock !");
    brelse(fsinfo->group_desc_bh);
    brelse(fsinfo->super_block_bh);
    sb->s_fs_info = NULL;
    kfree(fsinfo);
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

const struct super_operations ft_ops = {
    .statfs       = simple_statfs,
    .show_options = generic_show_options,
    .put_super = ft_put_super,
};

