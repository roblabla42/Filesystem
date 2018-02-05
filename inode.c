#include "fortytwofs.h"
#include <linux/buffer_head.h>
#include <linux/slab.h>

static const struct inode_operations ft_dir_inode_operations;

/*
 * Get the block description from a sb.
 */
//static struct ftfs_block_group *ft_get_group(struct super_block *sb/*, int block*/)
//{
//    // The (only, in ftfs) blockgroup is at byte 2048, and takes 128 bytes.
//    if ((bh = sb_bread(sb, block / sb->s_blocksize)) == NULL)
//        goto failed;
//
//    return sb->fs_info
//}

/*
 * Gets an inode from an sb and ino. If it doesn't already exist, read the data
 * from the sb.
 */

static int get_raw_inode(struct super_block *sb, ino_t ino,
        struct ftfs_inode **inode, struct buffer_head **bh)
{
    struct ftfs_fs_info *fsinfo;
    struct ftfs_block_group *group;
    int offset;
    unsigned bgd_idx;

    fsinfo = (struct ftfs_fs_info*)sb->s_fs_info;
    // Get block group
    bgd_idx = (ino - 1) / (sb->s_blocksize * 8);
    LOG("Getting inode from bgd idx %u", bgd_idx);
    if (bgd_idx < fsinfo->super_block->block_count / fsinfo->super_block->blocks_per_group) {
        group = fsinfo->group_desc[bgd_idx];
        offset = ((ino - 1) % (sb->s_blocksize * 8)) * sizeof(struct ftfs_inode);
        LOG("Getting inode from block %d, offset %d, blocksize %ld", group->inode_table_block, offset, sb->s_blocksize);
        LOG("block nbr %ld", group->inode_table_block + (offset / sb->s_blocksize));
        if ((*bh = sb_bread(sb, group->inode_table_block + (offset / sb->s_blocksize))) == NULL)
            return -ENOMEM;
        LOG("offset %ld on %ld", offset % sb->s_blocksize, (*bh)->b_size);
        *inode = (struct ftfs_inode*)((*bh)->b_data + (offset % sb->s_blocksize));
        return 0;
    } else {
        return -EINVAL;
    }
}

static int alloc_raw_inode(struct super_block *sb)
{
    struct ftfs_fs_info *fsinfo;
    struct buffer_head *bh;
    unsigned long *bitmap;
    int next;
    int i;

    fsinfo = (struct ftfs_fs_info*)sb->s_fs_info;
    for (i = 0; i < fsinfo->super_block->block_count / fsinfo->super_block->blocks_per_group; i++) {
        if ((bh = sb_bread(sb, fsinfo->group_desc[i]->inode_bitmap_block)) == NULL)
            return -ENOMEM;

        bitmap = (unsigned long*)bh->b_data;
        next = find_next_zero_bit(bitmap, bh->b_size * 8, 0);
        if (next < bh->b_size * 8)
            goto found;
        brelse(bh);
    }
    return -ENOSPC;
found:
    // TODO: Make sure nobody else claims our buffer !
    bh->b_data[next / 8] |= 1 << (next % 8);
    // TODO: use inode_count instead of sb->blocksize * 8
    LOG("Reserved block %ld", next + i * (sb->s_blocksize * 8));
    mark_buffer_dirty(bh);
    brelse(bh);
    return next + i * (sb->s_blocksize * 8);
}

/*
 * Assigns the right inode-operations, aops, and file-operations to inode
 * depending on its type, which is reflected in mode.
 */
void	ft_assign_operations_to_inode(struct inode *inode, umode_t mode)
{
	LOG("Assigning operations with mode %x (%x)", mode, mode & S_IFMT);
	switch (mode & S_IFMT)
	{
	case S_IFREG:           LOG("reg");
	    inode->i_op             = &ft_file_inode_operations;
	    inode->i_mapping->a_ops = &ft_aops;
	    inode->i_fop            = &ft_file_operations;
	    break;
	case S_IFDIR:           LOG("dir");
	    inode->i_op             = &ft_dir_inode_operations;
	    inode->i_mapping->a_ops = &ft_aops;
	    inode->i_fop            = &ft_dir_file_operations;
	    break;
	case S_IFLNK:           LOG("link");
	    ft_init_symlink_inode(inode);
	    inode->i_op             = &simple_symlink_inode_operations;
	    break;
	default:                LOG("special");
	    init_special_inode(inode, inode->i_mode, 0);
	    break;
	}
}


static struct inode *ft_new_inode(struct inode *dir, umode_t mode)
{
    // TODO: Clean-up in error handling
    struct inode *inode;
    struct ftfs_inode_info *ft_inode_info;
    int ino;

    inode = new_inode(dir->i_sb);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    ft_inode_info = kmalloc(sizeof(struct ftfs_inode_info), GFP_KERNEL);
    if (!ft_inode_info)
        return ERR_PTR(-ENOMEM);

    if ((ino = alloc_raw_inode(dir->i_sb)) < 0)
        return ERR_PTR(ino);
    inode_init_owner(inode, dir, mode);
    inode->i_ino = ino;
    inode->i_blocks = 0;
    inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
    inode->i_private = ft_inode_info;
    inode->i_generation = 0;
    memset(ft_inode_info->blocks, 0, sizeof(ft_inode_info->blocks));
    ft_assign_operations_to_inode(inode, mode);
    if (insert_inode_locked(inode) < 0)
        return ERR_PTR(-EIO);
    return inode;
}

struct inode *ft_get_inode(struct super_block *sb, ino_t ino)
{
    struct inode *inode;
    struct buffer_head *bh;
    struct ftfs_inode *ft_inode;
    struct ftfs_inode_info *ft_inode_info;
    int ret;

    LOG("Getting inode %ld", ino);
    inode = iget_locked(sb, ino);
    if (!inode) {
        LOG("Failed to acquire/lock a new inode");
        return ERR_PTR(-ENOMEM);
    }
    if (!(inode->i_state & I_NEW))
        return inode;
    LOG("New inode %ld", ino);

    if ((ret = get_raw_inode(sb, ino, &ft_inode, &bh)) != 0)
        goto failed;

    // Initialize inode. There are quite a few already-initialized fields. You
    // can check with by looking at inode_init_always from fs/inode.c

    // There are two ways to store custom information in an inode. One is to
    // allocate a structure containing the data, and store a ptr to it in the
    // inode. The other is to implement super_block->alloc_inode to allocate
    // a structure containing both the struct inode and the extra data, and then
    // using container_of. For simplicity's sake, let's use i_private for now.
    //
    // TODO: figure out how to delete this
    ft_inode_info = kmalloc(sizeof(struct ftfs_inode_info), GFP_KERNEL);
    if (!ft_inode_info) {
        LOG("Failed to allocate memory !");
        ret = -ENOMEM;
        goto failed;
    }
    memcpy(ft_inode_info->blocks, ft_inode->blocks, sizeof(ft_inode->blocks));
    inode->i_private = ft_inode_info;

    inode->i_mode = ft_inode->mode;
    inode->i_size = ft_inode->size;

    // UID and GID needs to be mapped to the correct user namespace. The
    // i_uid_write/i_gid_write functions take care of this.
    i_uid_write(inode, ft_inode->uid);
    i_gid_write(inode, ft_inode->gid);

    inode->i_atime.tv_sec = ft_inode->atime;
    inode->i_ctime.tv_sec = ft_inode->ctime;
    inode->i_mtime.tv_sec = ft_inode->mtime;
    inode->i_atime.tv_nsec = inode->i_ctime.tv_nsec = inode->i_mtime.tv_nsec = 0;
    set_nlink(inode, ft_inode->nlinks);
    ft_assign_operations_to_inode(inode, inode->i_mode);

    brelse(bh);
    unlock_new_inode(inode);
    return inode;

failed:
    iget_failed(inode);
    return ERR_PTR(ret);
}

int ft_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    struct ftfs_inode *ft_inode;
    struct buffer_head *bh;
    struct ftfs_inode_info *inode_info;
    int ret;

    if ((ret = get_raw_inode(inode->i_sb, inode->i_ino, &ft_inode, &bh)) != 0)
        return ret;
    inode_info = (struct ftfs_inode_info*)inode->i_private;
    ft_inode->mode = inode->i_mode;
    ft_inode->uid = i_uid_read(inode);
    ft_inode->gid = i_gid_read(inode);
    ft_inode->size = inode->i_size;
    ft_inode->atime = inode->i_atime.tv_sec;
    ft_inode->ctime = inode->i_ctime.tv_sec;
    ft_inode->mtime = inode->i_mtime.tv_sec;
    ft_inode->nlinks = inode->i_nlink;
    memcpy(ft_inode->blocks, inode_info->blocks, sizeof(ft_inode->blocks));
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

static int ftfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
    // TODO: create inode
    struct inode *inode = ft_get_inode(dir->i_sb, get_next_ino());

    if (!inode)
        return -ENOSPC;
    d_instantiate(dentry, inode);
    dget(dentry);
    dir->i_mtime = dir->i_ctime = current_time(inode);
    return 0;
}

static int ftfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct inode *inode;
    int err;

    inode = ft_new_inode(dir, mode);
    if (IS_ERR(inode))
        return PTR_ERR(inode);
    if ((err = ft_insert_inode_in_dir(dir, dentry->d_name.name, inode->i_ino))) {
        inode_dec_link_count(inode);
        unlock_new_inode(inode);
        iput(inode);
        return err;
    } else {
        unlock_new_inode(inode);
        d_instantiate(dentry, inode);
        return 0;
    }

    //return ftfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Creates a hard link by making the dentry point to an already existing inode,
 * and adds a dir_entry pointing to that inode's ino.
 *
 * The inode->i_link is incremented.
 */
static int ft_hard_link(struct dentry *target, struct inode *dir, struct dentry *dentry)
{
	struct inode	*target_inode = d_inode(target);
	int		err;

	target_inode->i_ctime = current_time(target_inode);
	if ((err = ft_insert_inode_in_dir(dir, dentry->d_name.name, target_inode->i_ino)))
		return err;
	inode_inc_link_count(target_inode);
	d_instantiate(dentry, target_inode);
	return 0;
}

struct lookup_ctx {
    ino_t ino;
    const char *name;
    size_t name_len;
};

static int lookup_emit(struct ftfs_dir *dir, void *data)
{
    struct lookup_ctx *ctx = (struct lookup_ctx*)data;
    if (dir->name_len == ctx->name_len &&
            memcmp(dir->name, ctx->name, dir->name_len) == 0) {
        ctx->ino = dir->inode;
        return 0;
    } else {
        return 1;
    }
}

static struct dentry *ft_lookup(struct inode *dir, struct dentry *dentry,
        unsigned flags)
{
    struct lookup_ctx ctx = {
        .ino = 0,
        .name = dentry->d_name.name,
        .name_len = dentry->d_name.len
    };
    struct inode *inode;

    LOG("Looking up %s in inode %ld", dentry->d_name.name, dir->i_ino);
    ft_iterate(dir, lookup_emit, NULL, &ctx);
    if (ctx.ino == 0) {
        return NULL;
    }
    LOG("Got inode number %ld", ctx.ino);
    inode = ft_get_inode(dir->i_sb, ctx.ino);
    if (IS_ERR(inode))
        return ERR_PTR(PTR_ERR(inode));
    // TODO: aufs use d_add and returns NULL. But everywhere I look, people seem
    // to be using d_splice_alias. The documentation is a bit lightweight. I'd
    // like to understand what it does better.
    return d_splice_alias(inode, dentry);
}

static int ftfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
    struct inode *inode;
    struct page *page;
    int err;

    LOG("RUNNING MKDIR FOR %s with mode %x", dentry->d_name.name, mode | S_IFDIR);
    inode = ft_new_inode(dir, mode | S_IFDIR);
    if (IS_ERR(inode))
        return PTR_ERR(inode);

    // Very simple
    LOG("Getting page");
    page = ft_get_page(inode, 0);
    if (IS_ERR(page))
        return PTR_ERR(page);
    // TODO: Lock page
    LOG("Locking page");
    lock_page(page);
    struct ftfs_dir *firstdir = page_address(page);
    LOG("Page at address %p", firstdir);
    firstdir->len = dir->i_sb->s_blocksize;
    firstdir->inode = 0;
    unlock_page(page);
    ft_put_page(page);

    if ((err = ft_insert_inode_in_dir(inode, ".", inode->i_ino)))
        goto err;
    if ((err = ft_insert_inode_in_dir(inode, "..", dir->i_ino)))
        goto err;
    if ((err = ft_insert_inode_in_dir(dir, dentry->d_name.name, inode->i_ino)))
        goto err;

    unlock_new_inode(inode);
    d_instantiate(dentry, inode);
    return 0;

err:
    inode_dec_link_count(inode);
    unlock_new_inode(inode);
    iput(inode);
    return err;
}

static const struct inode_operations ft_dir_inode_operations = {
    .create     = ftfs_create,
    .lookup     = ft_lookup,
    .link       = ft_hard_link,
    .unlink     = simple_unlink,
    /* .symlink    = ftfs_symlink, [> TODO <] */
    .mkdir      = ftfs_mkdir,
    .rmdir      = simple_rmdir,
    //.mknod      = ftfs_mknod,
    .rename     = simple_rename,
};
