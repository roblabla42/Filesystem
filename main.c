#include "fortytwofs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ntibi");
MODULE_DESCRIPTION("testelele");

#define LOG(x) printk(KERN_DEBUG "%s: %s\n", __FUNCTION__, x)

#define FT_ROOT_INODE    1
#define FORTYTWOFS_MAGIC 0x4242

// TODO: umount: ida_remove called for id=26 which is not allocated

static int __init ft_init(void)
{
    int ret = 0;

    LOG("Hi");
    ret = register_filesystem(&ft_type);
    return ret;
}

static void __exit ft_cleanup(void)
{
    LOG("Hi");
    unregister_filesystem(&ft_type);
    return ;
}

module_init(ft_init);
module_exit(ft_cleanup);
