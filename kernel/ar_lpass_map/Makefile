# Out-of-tree build of ar_lpass_map.ko. KDIR: the build tree of the board's kernel
# (prepared with `make modules_prepare`); cross-compiling also needs ARCH and
# CROSS_COMPILE, and KERNELRELEASE if the tree's release string differs from the board's.
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
