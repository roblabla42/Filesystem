#include "fortytwofs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ntibi");
MODULE_DESCRIPTION("testelele");

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
