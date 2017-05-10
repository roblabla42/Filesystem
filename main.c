#include "fortytwofs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ntibi");
MODULE_DESCRIPTION("testelele");

#define LOG(x) printk(KERN_DEBUG "%s: %s\n", __FUNCTION__, x)

#define FT_ROOT_INODE    1
#define FORTYTWOFS_MAGIC 0x4242


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
