#include "fortytwofs.h"
#include <linux/pagemap.h>

// Returns the page nbr of that inode, and make sure it is mapped in memory !
static struct page *ft_get_page(struct inode *inode, int nbr)
{
    struct page *page;

    page = read_mapping_page(inode->i_mapping, nbr, NULL);
    if (!IS_ERR(page))
        kmap(page);
    return page;
}

static void ft_put_page(struct page *page)
{
    kunmap(page);
    put_page(page);
}

static int ft_iterate(struct file *file, struct dir_context *ctx) {
    // Get the data associated with the file.
    //
    // There are two ways to get data. Address_space or sb_bread. Let's try to
    // go the address space route
    //
    // ctx->pos is completely controlled by the implementer. You don't have to
    // increment it one by one. This is useful in our case, since a directory
    // entry has a variable size.

    struct inode *inode = file_inode(file);
    struct page *page;
    struct ftfs_dir *dir;
    int current_page;
    void *kaddr;

    while (ctx->pos < inode->i_size) {
        current_page = ctx->pos / PAGE_SIZE;
        page = ft_get_page(inode, current_page);
        if (IS_ERR(page))
            return PTR_ERR(page);
        kaddr = page_address(page);
        do {
            dir = (struct ftfs_dir*)(kaddr + (ctx->pos % PAGE_SIZE));
            if (PAGE_SIZE - (ctx->pos % PAGE_SIZE) < dir->len) {
                // malformed directory entry !
                return -EIO;
            }
            // dir->inode == 0 means the entry is "unused". This can happen
            // when using rm.
            if (!dir->inode == 0) {
                if (!dir_emit(ctx, dir->name, dir->name_len, dir->inode,
                        DT_UNKNOWN)) {
                    ft_put_page(page);
                    return 0;
                }
            }
            ctx->pos += dir->len;
        } while (ctx->pos < inode->i_size &&
                ctx->pos / PAGE_SIZE < current_page);
        ft_put_page(page);
    }
    return 0;
}

const struct file_operations ft_dir_file_operations = {
    .iterate_shared = ft_iterate
};
