#include "fortytwofs.h"
#include <linux/buffer_head.h>
#include <uapi/linux/magic.h>
#include <linux/slab.h>
#include <linux/mpage.h>

static int ft_acquire_block(struct super_block *sb) {
    struct ftfs_fs_info *fsinfo;
    struct buffer_head *bh;
    unsigned long *bitmap;
    int next;
    int i;

    fsinfo = sb->s_fs_info;
    // TODO: Figure out what map_bh really does.
    // I've found that bread/brelse... come from BSD world actually. it's called
    // the buffercache : http://man.openbsd.org/bread.9
    //
    // Linux kernel implements a variant of it, but I'm not too sure about how
    // everything works.
    // TODO: I should lock this for sure !
    for (i = 0; i < fsinfo->super_block->block_count / fsinfo->super_block->blocks_per_group; i++) {
        LOG("Reading BGD @ %d", fsinfo->group_desc[i]->block_bitmap_block);
        if ((bh = sb_bread(sb, fsinfo->group_desc[i]->block_bitmap_block)) == NULL) {
            LOG("Failed to read BGD !");
            return -EIO;
        }
        bitmap = (unsigned long*)bh->b_data;
        next = find_next_zero_bit(bitmap, bh->b_size * 8, 0);
        if (next < bh->b_size * 8)
            goto finished;
        brelse(bh);
    }
    return -ENOSPC;
finished:
    // Make sure nobody else claims our buffer !
    bh->b_data[next / 8] |= 1 << (next % 8);
    LOG("Reserved block %ld", (1024 / sb->s_blocksize) + i * fsinfo->super_block->blocks_per_group + next);
    mark_buffer_dirty(bh);
    brelse(bh);
    // We don't zero-out the block here ! Instead, we expect the user to call
    // set_buffer_new, which asks the kernel to zero-out the buffer when it's
    // loaded into the page cache.
    //
    // TODO: We need to change this. The behavior is almost certainly wrong in
    // edge cases.
    // The first block is reserved
    return (1024 / sb->s_blocksize) + i * fsinfo->super_block->blocks_per_group + next;
}

#define FT_ACQUIRE_BLOCK(sb, blockptr, create, new, dirty, ptr) (create) ? ft_get_or_allocate_block((sb), (blockptr), new, (void(*)(void*))(dirty), (ptr)) : *(blockptr)

static int ft_get_or_allocate_block(struct super_block *sb, int *block, int *new_out, void (*mark_dirty)(void*), void *dirty_data) {
    int res;

    if (new_out)
        *new_out = 0;
    if (*block == 0) {
        if ((res = ft_acquire_block(sb)) < 0)
            return res;
        *block = res;
        if (new_out)
            *new_out = 1;
        mark_dirty(dirty_data);
    }
    return *block;
}

int ft_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create)
{
    struct ftfs_inode_info *ft_inode = (struct ftfs_inode_info*)inode->i_private;
    int block = 0;
    int new = 0;

    LOG("getting block %lu (%s)", iblock, create ? "create" : "read");
    if (iblock < 12)
    {
        // Only support allocation for direct blocks for now
        LOG("   direct blocks[%lu]", iblock);
        // If block is < 0, it's an error. If it's 0, we are read-only and block doesn't exist yet
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, &ft_inode->blocks[iblock], create, &new, mark_inode_dirty, inode)) <= 0)
            return block;
        LOG("       Lvl0 block[%d]", block);
        map_bh(bh, inode->i_sb, block);
        if (new)
            set_buffer_new(bh);
        return 0;
    }
    iblock -= 12;
    if (iblock < 0x100)
    {
        struct buffer_head *ibh;

        LOG("   indirect1 [12][%lu]", iblock);
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, &ft_inode->blocks[12], create, NULL, mark_inode_dirty, inode)) <= 0)
            return block;
        LOG("       Lvl0 block[%d]", block);
        ibh = sb_bread(inode->i_sb, block);
        if (!ibh)
            return -ENOMEM;
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, &((__le32*)ibh->b_data)[iblock], create, &new, mark_buffer_dirty, ibh)) <= 0)
            return block;
        LOG("       Lvl1 block[%d]", block);
        brelse(ibh);
        map_bh(bh, inode->i_sb, block);
        if (new)
            set_buffer_new(bh);
        return 0;
    }
    iblock -= 0x100;
    if (iblock < 0x10000)
    {
        struct buffer_head *ibh;
        __le32 *i1, *i2;

        LOG("   indirect2 [13][%lu][%lu]", iblock >> 8, iblock & 0xff);
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, &ft_inode->blocks[13], create, NULL, mark_inode_dirty, inode)) <= 0)
            return block;
        LOG("       Lvl0 block[%d]", block);
        ibh = sb_bread(inode->i_sb, block);
        if (!ibh)
            return -ENOMEM;
        i1 = &((__le32*)ibh->b_data)[iblock >> 8]; // 0xXX00
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, i1, create, NULL, mark_buffer_dirty, ibh)) <= 0)
            return block;
        LOG("       Lvl1 block[%d]", block);
        brelse(ibh);
        ibh = sb_bread(inode->i_sb, block);
        if (!ibh)
            return -ENOMEM;
        i2 = &((__le32*)ibh->b_data)[iblock & 0xff]; // 0x00XX
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, i2, create, &new, mark_buffer_dirty, ibh)) <= 0)
            return block;
        LOG("       Lvl2 block[%d]", block);
        brelse(ibh);
        map_bh(bh, inode->i_sb, block);
        if (new)
            set_buffer_new(bh);
        return 0;
    }
    iblock -= 0x10000;
    if (iblock < 0x1000000)
    {
        struct buffer_head *ibh;
        __le32 *i1, *i2, *i3;

        LOG("   indirect3 [14][%lu][%lu][%lu]", iblock >> 16, (iblock >> 8) & 0xff, iblock & 0xff);
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, &ft_inode->blocks[14], create, NULL, mark_inode_dirty, inode)) <= 0)
            return block;
        LOG("       Lvl0 block[%d]", block);
        ibh = sb_bread(inode->i_sb, block);
        if (!ibh)
            return -ENOMEM;
        i1 = &((__le32*)ibh->b_data)[iblock >> 16]; // 0xXX0000
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, i1, create, NULL, mark_buffer_dirty, ibh)) <= 0)
            return block;
        LOG("       Lvl1 block[%d]", block);
        brelse(ibh);
        ibh = sb_bread(inode->i_sb, block);
        if (!ibh)
            return -ENOMEM;
        i2 = &((__le32*)ibh->b_data)[(iblock >> 8) & 0xff]; // 0x00XX00
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, i2, create, NULL, mark_buffer_dirty, ibh)) <= 0)
            return block;
        LOG("       Lvl2 block[%d]", block);
        brelse(ibh);
        ibh = sb_bread(inode->i_sb, block);
        if (!ibh)
            return -ENOMEM;
        i3 = &((__le32*)ibh->b_data)[iblock & 0xff]; // 0x0000XX
        if ((block = FT_ACQUIRE_BLOCK(inode->i_sb, i3, create, &new, mark_buffer_dirty, ibh)) <= 0)
            return block;
        LOG("       Lvl3 block[%d]", block);
        brelse(ibh);
        map_bh(bh, inode->i_sb, block);
        if (new)
            set_buffer_new(bh);
        return 0;
    }
    return 0;
}

static int ft_readpage(struct file *file, struct page *page)
{
    return mpage_readpage(page, ft_get_block);
}

static int ft_writepage(struct page *page, struct writeback_control *wbc)
{
    return block_write_full_page(page, ft_get_block, wbc);
}

static int ft_write_begin(struct file *file, struct address_space *mapping,
        loff_t pos, unsigned len, unsigned flags, struct page **pagep,
        void **fsdata)
{
    return block_write_begin(mapping, pos, len, flags, pagep, ft_get_block);
}

// This is going to be where the actual reading/writing happens. The kernel will
// call those functions whenever it needs to pull data from the fs to the
// pagecache.
const struct address_space_operations ft_aops = {
    .readpage       = ft_readpage,
    .writepage      = ft_writepage,
    .write_begin    = ft_write_begin,
    .write_end      = generic_write_end
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
    size_t block_group_count;
    int i, j;
    int super_block_nr;

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
    super_block_nr = 1024 / blocksize;
    if ((bh = sb_bread(sb, super_block_nr)) == NULL)
        goto failed_fsinfo;
    fsinfo->super_block = (struct ftfs_super_block*)bh->b_data + (1024 % blocksize);
    fsinfo->super_block_bh = bh;

    block_group_count = fsinfo->super_block->block_count / fsinfo->super_block->blocks_per_group;
    if ((fsinfo->group_desc = kzalloc(sizeof(struct ftfs_block_group*) * block_group_count, GFP_KERNEL)) == NULL)
        goto failed_group_desc;
    if ((fsinfo->group_desc_bh = kzalloc(sizeof(struct buffer_head*) * block_group_count, GFP_KERNEL)) == NULL)
        goto failed_group_desc_bh;

    for (i = 0; i < block_group_count; i++) {
        // Get the block group info. The block group
        LOG("Acquiring group %d at block %d", i, fsinfo->super_block->blocks_per_group * i + super_block_nr + 1);
        if ((fsinfo->group_desc_bh[i] = sb_bread(sb, fsinfo->super_block->blocks_per_group * i + super_block_nr + 1)) == NULL)
            goto failed_super_block;
        fsinfo->group_desc[i] = (struct ftfs_block_group*)fsinfo->group_desc_bh[i]->b_data;
    }

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
    if (IS_ERR(root_inode)) {
        ret = PTR_ERR(root_inode);
        goto failed_super_block;
    }
    if (!(sb->s_root = d_make_root(root_inode)))
        goto failed_release;

    return 0;
cantfind_ftfs:
    LOG("error: can't find an ft filesystem on device %s, got %lx, blocksize=%ld.", sb->s_id, sb->s_magic, sb->s_blocksize);
failed_super_block:
    for (j = 0; j < i; j++) {
        brelse(fsinfo->group_desc_bh[j]);
    }
failed_group_desc:
    kfree(fsinfo->group_desc);
failed_group_desc_bh:
    kfree(fsinfo->group_desc_bh);
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
    size_t block_group_count;
    int i;

    LOG("Releasing superblock !");
    block_group_count = fsinfo->super_block->block_count / fsinfo->super_block->blocks_per_group;
    for (i = 0; i < block_group_count; i++)
        brelse(fsinfo->group_desc_bh[i]);
    kfree(fsinfo->group_desc_bh);
    kfree(fsinfo->group_desc);
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
    .write_inode = ft_write_inode,
    //.show_options = generic_show_options,
    .put_super = ft_put_super,
};

