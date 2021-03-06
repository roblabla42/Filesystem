#include "fortytwofs.h"
#include <linux/pagemap.h>
#include <linux/buffer_head.h>

// Returns the page nbr of that inode, and make sure it is mapped in memory !
struct page *ft_get_page(struct inode *inode, int nbr)
{
    struct page *page;

    page = read_mapping_page(inode->i_mapping, nbr, NULL);
    if (!IS_ERR(page))
        kmap(page);
    return page;
}

void ft_put_page(struct page *page)
{
    kunmap(page);
    put_page(page);
}

int ft_insert_inode_in_dir(struct inode *inode, const char *name, ino_t ino) {
    int current_page = 0;
    int pos;
    size_t record_len;
    struct ftfs_dir *dir;
    struct ftfs_dir *newdir;
    struct page *page;
    void *kaddr;
    int err = 0;
    int rec_len;

    LOG("Creating %s", name);
    // TODO: Error handling
    // TODO: LOOOOOTS of locking
    // They use i_size_write to lock inode->i_size, and they use lock_page
    record_len = sizeof(struct ftfs_dir) + strlen(name);
    // We go to i_size + PAGE_SIZE as to allocate a new page if there's no space
    // left
    while (current_page * PAGE_SIZE < inode->i_size + PAGE_SIZE) {
        page = ft_get_page(inode, current_page);
        if (IS_ERR(page))
            return PTR_ERR(page);
        lock_page(page);
        // TODO: Lock page ?
        kaddr = page_address(page);
        pos = 0;
        do {
            dir = (struct ftfs_dir*)(kaddr + pos);
            if (current_page * PAGE_SIZE == inode->i_size) {
                LOG("Allocating new page for direntry");
                goto got_it;
            }
            if (PAGE_SIZE - pos < dir->len) {
                LOG("Malformed direntry");
                err = -EIO;
                goto err_unlock;
            }
            if (dir->inode == 0 && record_len < dir->len) {
                LOG("Using empty inode for direntry");
                goto got_it;
            } else if (dir->inode != 0 && record_len < dir->len - (sizeof(struct ftfs_dir) + dir->name_len)) {
                LOG("Using unused space next to '%.*s' for direntry", dir->name_len, dir->name);
                goto got_it;
            }
            pos += dir->len;
        } while (pos < PAGE_SIZE);
        unlock_page(page);
        ft_put_page(page);
        current_page++;
    }
    return -EINVAL;

got_it:
    rec_len = dir->len;
    if ((err = __block_write_begin(page, pos, rec_len, ft_get_block)))
        goto err_unlock;
    if (dir->inode != 0) {
        // The space is after the current dir.
        dir->len = sizeof(struct ftfs_dir) + dir->name_len;
        newdir = (struct ftfs_dir*)(kaddr + pos + dir->len);
        newdir->inode = 0;
        newdir->len = rec_len - dir->len;
        LOG("Had len of %d, now old direntry is %d and new is %d", rec_len, dir->len, newdir->len);
        dir = newdir;
    }
    dir->name_len = strlen(name);
    memcpy(dir->name, name, strlen(name));
    dir->inode = ino;
    block_write_end(NULL, page->mapping, pos, rec_len, rec_len, page, NULL);
    if (pos + rec_len > inode->i_size) {
        inode->i_size = pos + rec_len;
        mark_inode_dirty(inode);
    }
    unlock_page(page);
err_unlock:
    ft_put_page(page);
    return err;
}

int ft_iterate(struct inode *inode, ft_iterator emit, loff_t *pos, void *data) {
    // Get the data associated with the file.
    //
    // There are two ways to get data. Address_space or sb_bread. Let's try to
    // go the address space route
    //
    // ctx->pos is completely controlled by the implementer. You don't have to
    // increment it one by one. This is useful in our case, since a directory
    // entry has a variable size.
    struct page *page;
    struct ftfs_dir *dir;
    int current_page;
    int dirty, result;
    void *kaddr;
    loff_t default_pos = 0;

    if (pos == NULL)
        pos = &default_pos;

    if (!S_ISDIR(inode->i_mode))
            return -ENOTDIR;

    while (*pos < inode->i_size) {
        current_page = *pos / PAGE_SIZE;
        page = ft_get_page(inode, current_page);
        if (IS_ERR(page))
            return PTR_ERR(page);
        kaddr = page_address(page);
        do {
            dir = (struct ftfs_dir*)(kaddr + (*pos % PAGE_SIZE));
            if (dir->len == 0 || PAGE_SIZE - (*pos % PAGE_SIZE) < dir->len) {
                LOG("Malformed direntry");
                return -EIO;
            }
            // dir->inode == 0 means the entry is "unused". This can happen
            // when using rm.
            if (dir->inode != 0) {
                LOG("%.*s is inode %d", dir->name_len, dir->name, dir->inode);
                dirty = 0;
                result = emit(dir, data, &dirty);
                if (dirty)
                    set_page_dirty(page);
                if (!result) {
                    ft_put_page(page);
                    return 0;
                }
            }
            *pos += dir->len;
        } while (*pos < inode->i_size &&
                *pos / PAGE_SIZE < current_page);
        ft_put_page(page);
    }
    return 0;
}

static int is_dir_not_empty_emit(struct ftfs_dir *dir, void *data, int *dirty)
{
	(void)dirty;
	if (dir->name_len < 1) {
		*(int *)data = -EIO;
		return 0;
	}
	if (!(
		(dir->name_len == 1 && strncmp(".", dir->name, 1) == 0)
		||
		(dir->name_len == 2 && strncmp("..", dir->name, 2) == 0)
	   )) {
		*(int *)data = -ENOTEMPTY;
		return 0;
	}
	return 1;
}

/*
 * Checks that a directory contains only the entries '.' and '..'
 * The checks are simply done by strcmp the name of the direntries
 * Returns 0 if empty, -ENOTEMPTY if not empty and other values on errors
 */
int ft_is_dir_not_empty(struct inode *inode)
{
	int result = 0;
	int err;

	err = ft_iterate(inode, is_dir_not_empty_emit, NULL, &result);
	if (err)
		return err;
	return result;
}

static int update_dotdot_emit(struct ftfs_dir *dir, void *data, int *dirty)
{
	if (dir->name_len == 2 && strncmp("..", dir->name, 2) == 0) {
		dir->inode = *(unsigned long long *)data;
		*dirty = 1;
		return 0;
	}
	return 1;
}

/*
 * Updates the '..' entry in dir to point to ino.
 * Used when moving a directory to make its '..' point to its new parent.
 *
 * The caller must not forget to update the links counts of the old
 * and new parents.
 */
int ft_update_dotdot(struct inode *dir, unsigned long long ino)
{
	return ft_iterate(dir, update_dotdot_emit, NULL, &ino);
}

static int readdir_dir_emit(struct ftfs_dir *dir, void *data, int *dirty)
{
    return dir_emit(data, dir->name, dir->name_len, dir->inode, DT_UNKNOWN);
}

static int ft_readdir(struct file *file, struct dir_context *ctx) {
    struct inode *inode = file_inode(file);
    return ft_iterate(inode, readdir_dir_emit, &ctx->pos, ctx);
}

const struct file_operations ft_dir_file_operations = {
    .iterate_shared = ft_readdir
};
