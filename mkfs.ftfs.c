#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include "fortytwofs_structs.h"

/*enum opts {
OPTS_HELP
};



int parse_opt(char *str) {
    if (str[i][1] == 'h' || str[i][)

}
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            opt = parse_opt_long(&argv[i][2]);
            if (opt < 0) {
                printf("Invalid option -- %s", opt);
                return 1;
            }
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            
        } else {
            if (drive == NULL)
                drive = argv[i];
            else {
                printf("Too many arguments");
                return 1;
            }
        }
*/

#define LOG_BLOCK_SIZE 0
#define BLOCKSIZE (1024 << LOG_BLOCK_SIZE)
#define BLOCKS_PER_GROUP (BLOCKSIZE * 8)
#define INODES_PER_GROUP (BLOCKSIZE * 8)

int write_super_block(int fd, struct stat *stat) {
    struct ftfs_super_block sb;

    memset(&sb, 0, sizeof(sb));
    sb.block_count = stat->st_size / BLOCKSIZE;
    sb.log_block_size = LOG_BLOCK_SIZE;
    sb.blocks_per_group = BLOCKS_PER_GROUP;
    sb.magic = 0x4242;
    write(fd, &sb, sizeof(sb));
    return 0;
}

int write_block_bitmap(int fd) {
    char bytes[1024];
    int i;

    memset(bytes, 0, sizeof(bytes));
    // The first 1024 + 3 blocks are always reserved, for the BGD, bitmaps, and table
    for (i = 0; i < (INODES_PER_GROUP * sizeof(struct ftfs_inode) / BLOCKSIZE) / 256; i++) {
        bytes[i] = 0xff;
    }
    bytes[i] = 0xe0;
    return write(fd, bytes, sizeof(bytes));
}

int write_inode_bitmap(int fd) {
    char bytes[1024];

    memset(bytes, 0, sizeof(bytes));
    return write(fd, bytes, sizeof(bytes));
}

int write_inode_table(int fd) {
    char bytes[INODES_PER_GROUP * sizeof(struct ftfs_inode)];

    memset(bytes, 0, sizeof(bytes));
    return write(fd, bytes, sizeof(bytes));
}

int write_block_group_descriptor(int fd, int bgd_block_nbr, struct stat *stat) {
    struct ftfs_block_group bgd;

    memset(&bgd, 0, sizeof(bgd));
    // 4 blocks reserved for BGD : the bgd block, 2 bitmap blocks, and table blocks
    bgd.block_bitmap_block = bgd_block_nbr + 1;
    bgd.inode_bitmap_block = bgd_block_nbr + 2;
    bgd.inode_table_block = bgd_block_nbr + 3;
    if (write(fd, &bgd, sizeof(bgd)) < 0)
        return -1;
    lseek(fd, SEEK_CUR, 1024 - sizeof(bgd));
    if (write_block_bitmap(fd) < 0)
        return -1;
    if (write_inode_bitmap(fd))
        return -1;
    if (write_inode_table(fd))
        return -1;
    return 0;
}

int write_root_folder(int fd, int root_block_nbr) {
    struct ftfs_block_group bgd;
    struct ftfs_inode inode;
    time_t timeval;

    lseek(fd, SEEK_SET, root_block_nbr * BLOCKSIZE);
    if (read(fd, &bgd, sizeof(bgd)) != sizeof(bgd))
        return -1;
    lseek(fd, SEEK_SET, bgd.inode_bitmap_block * BLOCKSIZE);

    timeval = time(NULL);
    memset(&inode, 0, sizeof(struct ftfs_inode));
    inode.mode = 040755;
    inode.uid = 0;
    inode.gid = 0;
    inode.size = 4096; // Default to one page
    inode.atime = timeval;
    inode.ctime = timeval;
    inode.mtime = timeval;
    inode.nlinks = 3; // /, . and ..
    inode.blocks[15];
    // Write inode 0
    if (write(fd, &inode, sizeof(struct ftfs_inode)) == -1) {
        perror("write_root_folder");
        return -1;
    }
    // Write the folder !
}

void usage(int argc, char **argv) {
    char *name;

    if (argc >= 1)
        name = argv[0];
    else
        name = "mkfs.ftfs";

    printf("Usage: %s device\n", name);
}

int main(int argc, char **argv) {
    int fd;
    struct stat stat;
    int i;

    if (argc < 2) {
        usage(argc, argv);
        return 1;
    }

    fd = open(argv[1], O_RDWR);
    if (fstat(fd, &stat) < 0) {
        perror(argv[0]);
        return 1;
    }
    lseek(fd, SEEK_SET, 1024);
    if (write_super_block(fd, &stat) != 0) {
        perror(argv[0]);
        return 1;
    }
    for (i = 2; i < stat.st_size / BLOCKSIZE; i += BLOCKS_PER_GROUP) {
        lseek(fd, SEEK_SET, i * BLOCKSIZE);
        write_block_group_descriptor(fd, i, &stat);
    }
    write_root_folder(fd, 2);
}
