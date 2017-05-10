#include "fortytwofs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ntibi");
MODULE_DESCRIPTION("testelele");

#define LOG(x) printk(KERN_DEBUG "%s: %s\n", __FUNCTION__, x)

#define FT_ROOT_INODE    1
#define FORTYTWOFS_MAGIC 0x4242

// FIXME
// ftfs_module_cleanup: unregistering filesystem
// ftfs_module_init: registering filesystem
// blk-mq: bad return on queue: -5
// blk_update_request: I/O error, dev loop0, sector 0

static int __init ftfs_module_init(void)
{
    int ret = 0;

    LOG("registering filesystem");
    ret = register_filesystem(&ft_type);
    return ret;
}

static void __exit ftfs_module_cleanup(void)
{
    LOG("unregistering filesystem");
    unregister_filesystem(&ft_type);
    return ;
}

module_init(ftfs_module_init);
module_exit(ftfs_module_cleanup);
