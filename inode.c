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
struct inode *ft_get_inode(struct super_block *sb, ino_t ino)
{
    struct inode *inode;
    struct buffer_head *bh;
    struct ftfs_block_group *group;
    struct ftfs_inode *ft_inode;
    int offset;
    struct ftfs_inode_info *ft_inode_info;
    int ret;

    LOG("Getting inode %ld", ino);
    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;
    LOG("New inode %ld", ino);

    ret = -ENOMEM;

    group = (struct ftfs_block_group*)((struct ftfs_fs_info*)sb->s_fs_info)->group_desc;
    offset = (ino - 1) * sizeof(struct ftfs_inode);
    LOG("Getting inode from block %d, offset %d, blocksize %ld", group->inode_table_block, offset, sb->s_blocksize);
    LOG("block nbr %ld", group->inode_table_block + (offset / sb->s_blocksize));
    if ((bh = sb_bread(sb, group->inode_table_block + (offset / sb->s_blocksize))) == NULL)
        goto failed;
    LOG("offset %ld on %ld", offset % sb->s_blocksize, bh->b_size);
    ft_inode = (struct ftfs_inode*)(bh->b_data + (offset % sb->s_blocksize));

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

    switch (inode->i_mode & S_IFMT)
    {
    case S_IFREG:           LOG("reg");
        inode->i_op             = &ft_file_inode_operations;
        inode->i_mapping->a_ops = &ft_aops;
        inode->i_fop            = &ft_file_operations;
        break;
    case S_IFDIR:           LOG("dir");
        inc_nlink(inode);
        inode->i_op             = &ft_dir_inode_operations;
        inode->i_mapping->a_ops = &ft_aops;
        inode->i_fop            = &ft_dir_file_operations;
        break;
    case S_IFLNK:           LOG("link");
        inode->i_op             = &simple_symlink_inode_operations;
        break;
    default:                LOG("special");
        init_special_inode(inode, inode->i_mode, 0);
        break;
    }

    brelse(bh);
    unlock_new_inode(inode);
    return inode;

failed:
    iget_failed(inode);
    return ERR_PTR(ret);
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

static int ftfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
    return ftfs_mknod(dir, dentry, mode | S_IFDIR, 0);
}

static int ftfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    return ftfs_mknod(dir, dentry, mode | S_IFREG, 0);
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

static const struct inode_operations ft_dir_inode_operations = {
    //.create     = ftfs_create,
    .lookup     = ft_lookup,
    .link       = simple_link,
    .unlink     = simple_unlink,
    /* .symlink    = ftfs_symlink, [> TODO <] */
    //.mkdir      = ftfs_mkdir,
    .rmdir      = simple_rmdir,
    //.mknod      = ftfs_mknod,
    .rename     = simple_rename,
};
