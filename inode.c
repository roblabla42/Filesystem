#include "fortytwofs.h"
#include <linux/buffer_head.h>
#include <linux/fsnotify.h>
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
    LOG("Reserved ino %ld", next + i * (sb->s_blocksize * 8));
    mark_buffer_dirty(bh);
    brelse(bh);
    return next + i * (sb->s_blocksize * 8) + 1;
}

/*
 * Assigns the right inode-operations, aops, and file-operations to inode
 * depending on its type, which is reflected in mode.
 * @inode:	the inode to assign operations to
 * @mode:	its mode
 * @rdev:	used only for special inodes
 * @creating:	a bool representing if the inode is being created or
 *		fetched from the disk
 */
static void	ft_assign_operations_to_inode(struct inode *inode,
						umode_t mode,
						dev_t rdev,
						bool creating)
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
	    if (!creating)
		    ft_init_symlink_inode(inode);
	    break;
	default:                LOG("special");
	    /* BLK - CHR - FIFO */
	    LOG("Mode is : %x", inode->i_mode);
	    inode->i_op             = &ft_special_inode_operations;
	    init_special_inode(inode, inode->i_mode, rdev);
	    break;
	}
}

/*
 * Allocates a new inode and gives it the right inode-operations
 * @dir:	the parent of this inode
 * @mode:	its mode
 * @rdev:	used only for special inodes
 */
struct inode *ft_new_inode(struct inode *dir, umode_t mode, dev_t rdev)
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
    ft_assign_operations_to_inode(inode, mode, rdev, true);
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
    dev_t rdev;
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
    /* block_count is in units of 512 bytes, convert it to blocks */
    inode->i_blocks = DIV_ROUND_UP(ft_inode->block_count * 512, sb->s_blocksize);
    set_nlink(inode, ft_inode->nlinks);
    /* If inode is a fifo, its rdev is stored in the blocks array.
     * We get it and let assign_operation decide if it's actually one or not */
    rdev = ft_inode->blocks[0];
    ft_assign_operations_to_inode(inode, inode->i_mode, rdev, false);

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

    LOG("Writing inode ino %lu", inode->i_ino);
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
    ft_inode->block_count = inode->i_blocks * (inode->i_sb->s_blocksize / 512);
    /* We're saving to much blocks, but i'll start caring when ext2 will
     * consider not having braindamaged units */
    if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
    	/* We store the rdev in the blocks array */
	ft_inode->blocks[0] = inode->i_rdev;
	ft_inode->blocks[1] = 0; /* Mark array termination */
    } else {
	/* Regular file/dir, or symlink */
	memcpy(ft_inode->blocks, inode_info->blocks, sizeof(ft_inode->blocks));
    }
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

/* Insert the inode in the dir, mark it as ready and instantiate its d_entry */
int ftfs_finish_inode_creation(struct inode *inode, struct inode *dir,
					struct dentry *dentry)
{
	int err;

	err = ft_insert_inode_in_dir(dir, dentry->d_name.name, inode->i_ino);
	if (err) {
		inode_dec_link_count(inode);
		unlock_new_inode(inode);
		iput(inode);
		return err;
	} else {
		unlock_new_inode(inode);
		d_instantiate(dentry, inode);
		mark_inode_dirty(inode);
		return 0;
	}
}

/* User wants to create a special inode */
static int ftfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode;

	inode = ft_new_inode(dir, mode, dev);
	if (IS_ERR(inode))
	    return PTR_ERR(inode);

	return ftfs_finish_inode_creation(inode, dir, dentry);
}

/* User wants to create a new inode in a dir */
static int ftfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	/*
	 * We just create a regular mknod :)
	 * excl is ignored, don't know what it's for anyway
	 */
	return ftfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

/*
 * Creates a hard link by making the dentry point to an already existing inode,
 * and adds a dir_entry pointing to that inode's ino.
 *
 * The inode->i_link is incremented.
 */
static int __ft_hard_link(struct dentry *target, struct inode *dir, struct dentry *dentry)
{
	struct inode	*target_inode = d_inode(target);
	int		err;

	target_inode->i_ctime = current_time(target_inode);
	if ((err = ft_insert_inode_in_dir(dir, dentry->d_name.name, target_inode->i_ino)))
		return err;
	inode_inc_link_count(target_inode);
	return 0;
}

/* Wrapper of __ft_hard_link wich also calls d_instanciate(). */
static int ft_hard_link(struct dentry *target, struct inode *dir, struct dentry *dentry)
{
	int err;

	if ((err = __ft_hard_link(target, dir, dentry)))
		return err;
	d_instantiate(dentry, target->d_inode);
	return 0;
}

struct lookup_ctx {
    ino_t ino;
    const char *name;
    size_t name_len;
};

static int lookup_emit(struct ftfs_dir *dir, void *data, int *dirty)
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
    inode = ft_new_inode(dir, mode | S_IFDIR, 0);
    if (IS_ERR(inode))
        return PTR_ERR(inode);

    // Initialize the direntry structure so ft_insert_inode_in_dir works.
    LOG("Getting page");
    page = ft_get_page(inode, 0);
    if (IS_ERR(page))
        return PTR_ERR(page);
    LOG("Locking page"); // TODO: Lock page ?
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

struct ft_unlink_ctx {
    char *name;
    int inode;
};

static int ft_unlink_emit(struct ftfs_dir *dir, void *data, int *dirty) {
    struct ft_unlink_ctx *ctx = (struct ft_unlink_ctx*)data;
    if (strlen(ctx->name) == dir->name_len && strncmp(ctx->name, dir->name, dir->name_len) == 0) {
        ctx->inode = dir->inode;
        *dirty = 1;
        dir->inode = 0;
        return 0;
    } else {
        return 1;
    }
}

// The actual deleting of the inode is done indirectly by
// super_operations->evict_inode. this will just decrease the link count. The
// VFS will take care of calling evict_inode if the link count hits 0.
static int ft_unlink(struct inode *inode, struct dentry *dentry)
{
    struct ft_unlink_ctx ctx;
    loff_t off = 0;
    int ret;

    LOG("Getting page");
    ctx.inode = 0;
    ctx.name = dentry->d_name.name;
    // First, remove from the directory
    if ((ret = ft_iterate(inode, ft_unlink_emit, 0, &ctx)) < 0)
        return ret;
    // If we haven't found anything, return ENOENT
    if (ctx.inode == 0)
        return -ENOENT;

    // Get the inode
    struct inode *child_inode = ft_get_inode(inode->i_sb, ctx.inode);
    if (child_inode == NULL)
        return -ENOSPC;

    // Decrease the hard link count
    inode_dec_link_count(child_inode);

    return 0;
}

/* Changes an inode's name and location */
static int ft_rename(	struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	int err;
	struct inode *new_inode = new_dentry->d_inode;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL; /* We don't support those other fancy flags */

	/* If destination already exists, delete it */
	if (new_inode)
	{
		if (S_ISDIR(new_inode->i_mode))
			/* TODO support dir -> empty dir
			 * 1) check destination is empty dir
			 * 2) rmdir
			 * 3) ??? */
			return -EISDIR;
		err = ft_unlink(new_dir, new_dentry);
		if (err)
			return err;
		/* The job done by vfs_unlink() usually */
		fsnotify_link_count(new_inode);
		d_delete(new_dentry);
	}
	/* Simply add it in the new_dir and remove it from its current dir */
	err = __ft_hard_link(old_dentry, new_dir, new_dentry);
	if (err)
		return err;
	return ft_unlink(old_dir, old_dentry);

	/*
	 * TODO If we're moving a directory we must update its '..' direntry
	 * to point to its new parent
	 */
}

static const struct inode_operations ft_dir_inode_operations = {
    .create     = ftfs_create,
    .lookup     = ft_lookup,
    .link       = ft_hard_link,
    .unlink     = ft_unlink,
    .symlink    = ft_symlink,
    .mkdir      = ftfs_mkdir,
    .rmdir      = simple_rmdir,
    .mknod      = ftfs_mknod,
    .rename     = ft_rename,
};
