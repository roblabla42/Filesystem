obj-m += ftfs.o
ftfs-objs += main.o inode.o fortytwofs.o dir.o symlink.o

all: ftfs mkfs.ftfs

clean: ftfs-clean mkfs-clean

ftfs:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

ftfs-clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

mkfs.ftfs: mkfs.o
	$(CC) $^ -o $@

mkfs-clean:
	rm -f mkfs.ftfs
