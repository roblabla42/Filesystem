#include "fortytwofs.h"

static const struct inode_operations ft_dir_inode_operations;

struct inode *ft_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, ino_t ino)
{
    struct inode *inode;

    LOG("Hi");
    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;

    LOG("new inode");
    inode_init_owner(inode, dir, mode);
    inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
    switch (inode->i_mode & S_IFMT)
    {
    case S_IFREG:           LOG("reg");
        inode->i_op             = &ft_file_inode_operations;
        inode->i_mapping->a_ops = &ft_aops;
        inode->i_fop            = &ft_file_operations;
        break;
    case S_IFDIR:           LOG("dir");
        inode->i_op             = &ft_dir_inode_operations;
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

static int ftfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
    struct inode *inode = ft_get_inode(dir->i_sb, dir, mode, get_next_ino());

    if (!inode)
        return -ENOSPC;
    d_instantiate(dentry, inode);
    dget(dentry);
    dir->i_mtime = dir->i_ctime = CURRENT_TIME;
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

static const struct inode_operations ft_dir_inode_operations = {
    .create     = ftfs_create,
    .lookup     = simple_lookup,
    .link       = simple_link,
    .unlink     = simple_unlink,
    /* .symlink    = ftfs_symlink, [> TODO <] */
    .mkdir      = ftfs_mkdir,
    .rmdir      = simple_rmdir,
    .mknod      = ftfs_mknod,
    .rename     = simple_rename,
};
