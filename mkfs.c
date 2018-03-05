/* Format an image file to ftfs format */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

/* typedefs because we're not in the kernel */
typedef uint8_t  __u8;
typedef uint16_t __le16;
typedef uint32_t __le32;

#include "ftfs_structs.h"

#define MKFS_START_OFFSET 	1024 /* Boot record */
#define MKFS_BLOCK_SIZE   	1024
#define MKFS_BLOCK_SIZE_LOG	0   /* 1024 << 0 = 1024 */

/* To compute the size of the inodes table */
#define MKFS_INODES_PER_TABLE_BLOCK	(MKFS_BLOCK_SIZE / sizeof(struct ftfs_inode))

/* To compute the size of the block group descriptor table */
#define MKFS_GROUP_DESC_PER_TABLE_BLOCK	(MKFS_BLOCK_SIZE / sizeof(struct ftfs_block_group))

#define MKFS_BLOCKS_PER_GROUP		(MKFS_BLOCK_SIZE * 8) /* max of block bitmap block */
#define MKFS_INODES_TABLE_BLOCKS	(MKFS_BLOCKS_PER_GROUP / 16) /* let's use 1/16 of the blocks for the inode table */
#define MKFS_INODES_PER_GROUP		(MKFS_INODES_TABLE_BLOCKS * MKFS_INODES_PER_TABLE_BLOCK)
#define MKFS_GROUP_SIZE			(MKFS_BLOCKS_PER_GROUP * MKFS_BLOCK_SIZE)

/* The smallest group we support : it has only 1 data block */
#define MKFS_SMALLEST_GROUP_SIZE	( MKFS_BLOCK_SIZE /* block bitmap */ \
					+ MKFS_BLOCK_SIZE /* inode bitmap */ \
					+ MKFS_INODES_TABLE_BLOCKS * MKFS_BLOCK_SIZE /* inode table */ \
					+ MKFS_BLOCK_SIZE /* only 1 data block */ \
					)

/* Number of blocks in a group that are not data */
#define MKFS_GROUP_OVERHEAD_BLOCKS	( 1 /* block bitmap */	\
					+ 1 /* inode bitmap */	\
					+ MKFS_INODES_TABLE_BLOCKS /* inode table */	\
					)

/* The number of data blocks in a simple empty group.
 * This value is not accurate for the first group, where you also need to remove the blocks for the sb and bgd */
#define MKFS_FREE_BLOCKS_PER_GROUP	(MKFS_BLOCKS_PER_GROUP - MKFS_GROUP_OVERHEAD_BLOCKS)


/* The minimum size of the image we have to format */
#define MKFS_MIN_IMAGE_SIZE						\
	( MKFS_START_OFFSET	/* Boot record */			\
	+ MKFS_BLOCK_SIZE	/* Superblock */			\
	+ MKFS_BLOCK_SIZE	/* Block group descriptor table */	\
	+ MKFS_SMALLEST_GROUP_SIZE					\
	)

/* Offset to the block group descriptors table from the start of the file */
#define MKFS_GROUP_TABLE_OFFSET					\
	( MKFS_START_OFFSET /* Boot record */			\
	+ MKFS_BLOCK_SIZE /* Superblock */			\
	)

#define RETURN_IF(__param)		\
{					\
	int __err;			\
	__err = __param;		\
	if (err) {			\
		return(err);		\
	}				\
}

#define EXIT_IF_PERROR(__param)				\
{							\
	if (__param) {					\
		perror(__FILE__ ": '" #__param "'");	\
		exit(EXIT_FAILURE);			\
	}						\
}

/* To write zeros to a full block */
char	zero_block[MKFS_BLOCK_SIZE] = {0};

struct ftfs_super_block ft_sb = {
	.inodes_count      = 0,
	.block_count       = 0,
	.free_blocks_count = 0,
	.free_inodes_count = 0,
	.log_block_size    = MKFS_BLOCK_SIZE_LOG,
	.blocks_per_group  = MKFS_BLOCKS_PER_GROUP,
	.inodes_per_group  = MKFS_INODES_PER_GROUP,
	.magic             = FORTYTWOFS_MAGIC,
};

struct	ftfs_inode	root_inode = {
	.mode		= S_IFDIR | S_IRWXU,
	.uid		= 0, /* superuser */
	.size		= MKFS_BLOCK_SIZE, /* take only 1 block */
	.atime		= 0,
	.ctime		= 0,
	.mtime		= 0,
	.gid		= 0, /* superuser */
	.nlinks		= 2, /* the '.' and '..' */
	.block_count	= MKFS_BLOCK_SIZE / 512, /* because units of 512 -_- */
	.blocks		= { 0 }, /* this is dynamic */
};

struct	mkfs_root_dir {
	struct ftfs_dir	dot;
	char		dot_name[4];
	struct ftfs_dir	dotdot;
	char		dotdot_name[4];
};

struct	mkfs_root_dir	root_dir = {
	.dot = {
		.inode		= 2,
		.len		= 12,
		.name_len	= 1,
	},
	.dot_name	= ".",
	.dotdot	= {
		.inode		= 2,
		.len		= MKFS_BLOCK_SIZE - 12, /* take all the remaining space */
		.name_len	= 2,
	},
	.dotdot_name	= "..",
};

struct mkfs_infos {
	off_t	file_size;	/* remaning file size */
	int	block_groups_count;
	int	block_groups_table_size; /* in blocks */
	off_t	last_block_group_size; /* in blocks */
};

/* Fill a block with zeroes */
void	write_zero_block(int fd, int block_nbr)
{
	off_t	saved_pos;

	/* Save the postion in the file */
	saved_pos = lseek(fd, 0, SEEK_CUR);
	EXIT_IF_PERROR(saved_pos < 0);

	EXIT_IF_PERROR(lseek(fd, block_nbr * MKFS_BLOCK_SIZE, SEEK_SET) < 0);
	EXIT_IF_PERROR(write(fd, &zero_block, MKFS_BLOCK_SIZE) < 0);

	/* Restore saved pos */
	EXIT_IF_PERROR(lseek(fd, saved_pos, SEEK_SET) < 0);
}

/* Reserve the first n bits of the bitmap in the block block_nbr */
void	bitmap_reserve_first_bits(int fd, int block_nbr, int n)
{
	off_t	saved_pos;
	char	full_byte = 0xFF;
	char	byte = 0;

	/* Save the postion in the file */
	saved_pos = lseek(fd, 0, SEEK_CUR);
	EXIT_IF_PERROR(saved_pos < 0);

	EXIT_IF_PERROR(lseek(fd, block_nbr * MKFS_BLOCK_SIZE, SEEK_SET) < 0);

	while (n >= 8) {
		/* Just write some 0xFF */
		EXIT_IF_PERROR(write(fd, &full_byte, 1) < 0);
		n -= 8;
	}
	if (n != 0) {
		/* We construct the last byte */
		while (n--)
			byte = ((byte << 1) | 1);
		EXIT_IF_PERROR(write(fd, &byte, 1) < 0);
	}

	/* Restore saved pos */
	EXIT_IF_PERROR(lseek(fd, saved_pos, SEEK_SET) < 0);
}

void	create_root_inode(int fd, struct ftfs_block_group *bg)
{
	off_t	saved_pos;

	/* Save the postion in the file */
	saved_pos = lseek(fd, 0, SEEK_CUR);
	EXIT_IF_PERROR(saved_pos < 0);

	/* Compute root inode's block address */
	root_inode.blocks[0] = bg->inode_table_block + MKFS_INODES_TABLE_BLOCKS;

	/* Write the directory */
	EXIT_IF_PERROR(lseek(fd, root_inode.blocks[0] * MKFS_BLOCK_SIZE, SEEK_SET) < 0);
	EXIT_IF_PERROR(write(fd, &root_dir, sizeof(root_dir)) < 0);

	/* Write the inode in the inode_table[1] */
	EXIT_IF_PERROR(lseek(fd, bg->inode_table_block * MKFS_BLOCK_SIZE + sizeof(struct ftfs_inode), SEEK_SET) < 0);
	EXIT_IF_PERROR(write(fd, &root_inode, sizeof(struct ftfs_inode)) < 0);

	/* Restore saved pos */
	EXIT_IF_PERROR(lseek(fd, saved_pos, SEEK_SET) < 0);
}

/*
 * Compute the number of block group we will have to create,
 * accounting for the size of the block group descriptor table
 * which grows proportionally.
 */
void	compute_block_group_count(struct mkfs_infos *infos)
{
	off_t	remaining_size = infos->file_size;

	remaining_size -= MKFS_BLOCK_SIZE /* superblock */
				+ MKFS_BLOCK_SIZE; /* first blockgroup table block */

	infos->block_groups_count = 0;
	infos->block_groups_table_size = 1; /* At least 1 block */
	while (remaining_size > MKFS_GROUP_SIZE) {
		infos->block_groups_count++;
		remaining_size -= MKFS_GROUP_SIZE;
		if (infos->block_groups_count % MKFS_GROUP_DESC_PER_TABLE_BLOCK == 0) {
			/* The table must take 1 more block */
			if (remaining_size < MKFS_BLOCK_SIZE) {
				/* Not even enough space to make it grow */
				remaining_size = 0;
				break;
			} else {
				infos->block_groups_table_size++;
				remaining_size -= MKFS_BLOCK_SIZE;
			}
		}
	}
	if (remaining_size > 0) {
		/* Last group is truncated */
		if (remaining_size < MKFS_SMALLEST_GROUP_SIZE) {
			/* Not enough to make a valid group inside it */
			infos->last_block_group_size = 0;
		 } else {
			infos->block_groups_count++;
			infos->last_block_group_size = remaining_size / MKFS_BLOCK_SIZE;
		}
	}
}

/*
 * Write the block groups and the block group table in the image,
 * and fill the superblock structure accordingly
 */
void	write_block_groups(int fd, struct ftfs_super_block *sb, struct mkfs_infos *infos)
{
	int			gp = 0;
	struct ftfs_block_group	bg = { 0 };
	off_t			current_block;

	/* Go to the table */
	EXIT_IF_PERROR(lseek(fd, MKFS_GROUP_TABLE_OFFSET, SEEK_SET) < 0);
	current_block = MKFS_GROUP_TABLE_OFFSET / MKFS_BLOCK_SIZE + infos->block_groups_table_size;

	while (gp < infos->block_groups_count) {
		bg.block_bitmap_block = current_block++;
		bg.inode_bitmap_block = current_block++;
		bg.inode_table_block = current_block++;
		if (!(gp == infos->block_groups_count - 1 && infos->last_block_group_size)) {
			bg.free_blocks_count = MKFS_FREE_BLOCKS_PER_GROUP;
			bg.free_inodes_count = MKFS_INODES_PER_GROUP;
		} else {
			/* We're creating a truncated group */
			bg.free_blocks_count = infos->last_block_group_size - MKFS_GROUP_OVERHEAD_BLOCKS;
			bg.free_inodes_count = MKFS_INODES_PER_GROUP;
		}
		write_zero_block(fd, bg.block_bitmap_block);
		write_zero_block(fd, bg.inode_bitmap_block);
		if (gp == 0) {
			int	special_overhead_blocks;
			int	special_overhead_inodes;

			/* The very first group has a certain amount of blocks occupied by the sb and bgd which
			 * must be reflected in the free_blocks_count of the group and reserved in the bitmap. */
			special_overhead_blocks = 1 /* sb */
						+ infos->block_groups_table_size /* bgd */
						+ 1; /* the root inode data */
			bg.free_blocks_count -= special_overhead_blocks;

			/* Same thing goes for the inodes, where we must reserve the first 10 ino */
			special_overhead_inodes = 10;
			bg.free_inodes_count -= special_overhead_inodes;

			create_root_inode(fd, &bg);

			bitmap_reserve_first_bits(fd, bg.block_bitmap_block, MKFS_GROUP_OVERHEAD_BLOCKS + special_overhead_inodes);
			bitmap_reserve_first_bits(fd, bg.inode_bitmap_block, special_overhead_inodes);
		} else {
			bitmap_reserve_first_bits(fd, bg.block_bitmap_block, MKFS_GROUP_OVERHEAD_BLOCKS);
		}
		EXIT_IF_PERROR(write(fd, &bg, sizeof(struct ftfs_block_group)) < 0);
		sb->inodes_count += MKFS_INODES_PER_GROUP;
		sb->free_inodes_count += bg.free_inodes_count;
		sb->block_count += MKFS_BLOCKS_PER_GROUP;
		sb->free_blocks_count += bg.free_blocks_count;

		gp++;
		current_block = gp * MKFS_BLOCKS_PER_GROUP;
	}
}

void	write_super_block(int fd, struct ftfs_super_block *sb)
{
	/* Go to the 1024'th byte */
	EXIT_IF_PERROR(lseek(fd, MKFS_START_OFFSET, SEEK_SET) < 0);

	EXIT_IF_PERROR(write(fd, sb, sizeof(struct ftfs_super_block)) < 0);
}

/* stat -path- and return its file_size */
off_t stat_image_file(char *path)
{
	struct stat mystat;

	EXIT_IF_PERROR(stat(path, &mystat));
	if (mystat.st_size < MKFS_MIN_IMAGE_SIZE) {
		printf("Error: your image must be at least %ld octets\n", MKFS_MIN_IMAGE_SIZE);
		exit(EXIT_FAILURE);
	}

	return mystat.st_size;
}

int main(int ac, char **av)
{
	int			err;
	int			fd;
	struct mkfs_infos	infos = { 0 };

	if (ac < 2)
	{
		printf("usage : %s image\n", av[0]);
		return (EXIT_FAILURE);
	}
	infos.file_size = stat_image_file(av[1]);
	EXIT_IF_PERROR((fd = open(av[1], O_WRONLY)) < 0);

	/* Advance past the boot record */
	EXIT_IF_PERROR(lseek(fd, MKFS_START_OFFSET, SEEK_SET) < 0);
	infos.file_size -= MKFS_START_OFFSET;

	compute_block_group_count(&infos);

	printf("Creating %d block groups\n", infos.block_groups_count);

	write_block_groups(fd, &ft_sb, &infos);

	write_super_block(fd, &ft_sb);

	printf("Block size : %d\n"
		"%ld free inodes\n"
		"%ld free blocks\n",
		MKFS_BLOCK_SIZE,
		ft_sb.free_inodes_count,
		ft_sb.free_blocks_count);

	return (EXIT_SUCCESS);
}
