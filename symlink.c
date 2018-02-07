#include "fortytwofs.h"
#include <linux/stat.h>
#include <linux/namei.h>

/*
 * There is two types of symlinks : slow symlinks and fast symlinks.
 * For slow symlink, the path pointed is stored in the blocks, just like a
 * regular file.
 * If the name of the pointed path is short enough, the inode has no blocks at
 * all, and instead we store the string in the i_block array directly as if it
 * was a char *. Since i_block is 15 * 4 in size, the pointed path of a
 * fast symlink can be at most 60 char (59 + '\0').
 * We recognize a fast symlink by its i_blocks count being 0, and its i_size
 * is set to the length of the pointed path string in i_block.
 */

/* The maximum pahtlen of a fast symlink, not including the '\0' at the end */
#define FTFS_FAST_SYMLINK_MAXPATH (sizeof(((struct ftfs_inode_info *)0)->blocks) - 1)

/* Test if an inode is a fast symlink or not */
static inline int ft_is_fast_symlink(struct inode *inode)
{
	return (S_ISLNK(inode->i_mode) && inode->i_blocks == 0);
}

/*
 * Helper function to work with fast symlinks
 * Returns the inode->i_private->blocks as a char *
 */
static inline char *ft_fast_symlink_path(struct inode *inode)
{
	struct ftfs_inode_info *infos;

	infos = ft_get_inode_info(inode);
	if (WARN(infos == NULL, "The fastsymlink has no inode->i_private"))
		return NULL;
	return ((char *)&infos->blocks);
}

/*
 * The operations of a symlink inode
 */
const struct inode_operations ft_fast_symlink_inode_operations = {
	.get_link	= simple_get_link,
	.getattr	= simple_getattr,
};

const struct inode_operations ft_slow_symlink_inode_operations = {
	.get_link	= page_get_link,
	.getattr	= simple_getattr,
};

static void init_fast_symlink_inode(struct inode *inode)
{
	char *path_str = ft_fast_symlink_path(inode);

	inode->i_link = path_str;
	/* Add a '\0' at the end */
	nd_terminate_link(path_str, inode->i_size, FTFS_FAST_SYMLINK_MAXPATH);
	inode->i_op = &ft_fast_symlink_inode_operations;
}

static void init_slow_symlink_inode(struct inode *inode)
{
	/* Let vfs's page_get_link() read the link in the blocks */
	inode_nohighmem(inode);
	inode->i_mapping->a_ops = &ft_aops;
	inode->i_op = &ft_slow_symlink_inode_operations;
}

/* Initiate a symlink inode, preparing its i_link and operations.
 * Called only when fetching inode from disk */
int	ft_init_symlink_inode(struct inode *inode)
{
	if (WARN(!S_ISLNK(inode->i_mode), "inode passed is not a symlink"))
		return -EINVAL;
	if (ft_is_fast_symlink(inode))
		init_fast_symlink_inode(inode);
	else
		init_slow_symlink_inode(inode);
	return 0;
}

/* User wants to create a symlink */
int	ft_symlink(struct inode *dir, struct dentry *dentry,
			const char *symname)
{
	struct inode	*inode;
	size_t		symname_len;
	int		err;

	symname_len = strlen(symname);
	if (symname_len + 1 > dir->i_sb->s_blocksize)
		return -ENAMETOOLONG; /* symname takes more than 1 block */
	inode = ft_new_inode(dir, S_IFLNK | S_IRWXUGO);
	if (IS_ERR(inode))
		return (PTR_ERR(inode));
	if (symname_len + 1 > FTFS_FAST_SYMLINK_MAXPATH) {
		/* slow symlink, copy symname to the first block */
		/* TODO increment the block_count in the function aquiring
		 * the block instead */
		inode->i_blocks = 1;
		init_slow_symlink_inode(inode);
		err = page_symlink(inode, symname, symname_len + 1);
		if (WARN(err, "Writing slow symlink's symname failed")) {
			iget_failed(inode);
			return err;
		}
	} else {
		/* fast symlink, copy symname in the i_block array */
		memcpy(ft_fast_symlink_path(inode), symname, symname_len + 1);
		inode->i_size = symname_len;
		init_fast_symlink_inode(inode);
	}
	mark_inode_dirty(inode);
	return ftfs_finish_inode_creation(inode, dir, dentry);
}
