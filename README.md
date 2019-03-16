# ext2 filesystem kernel module

Re-implementation of the ext2 filesystem module for the linux kernel. School project at 42.

### Goals

The main goal is to understand the VFS layer of linux that enables support of every filesystem, by implementing 
our own. 

This forced us to understand and work with superblocks/inodes/rights/links, using linux's vfs and block device api, 
and reading a *looooot* of linux source code to understand what those obscure non-documented functions do.

In the end we get a filesystem module that can

* Read and write files
* Create and delete files
* Create and delete directories
* Support hard and symbolic links
* Handle user permissions

### Build

Just run `make`. It will use the Makefile in `/lib/modules/` to build the `ftfs.ko` module.

To load it in the kernel, run

```shell
# insmod ./ftfs.ko
```

### Format

We included a `mkfs.ftfs` tool to format a disk image to the ext2 format. 

To run it first create an empty file image

```text
$ dd if=/dev/zero of=DISK.img bs=1024 count=4096
```

and format it with

```shell
$ ./mkfs.ftfs DISK.img
```

You can then mount your newly created filesystem in /mnt with

```shell
# mount -o loop -t fortytwofs /mnt DISK.img
```

##### Final grade

**105 / 100**