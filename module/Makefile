#######################################################
## Makefile is used by the kernel build when building
## the kernel module.  It's invoked from inside
## "makefile" when 'make modules' is run
########################################################

#
# copied /usr/src/redhat/SOURCES/kernel-2.6.9-i686-smp.config .config
# added this line for SMP support
#
#	CFLAGS += -D__SMP__ -DSMP -DMODVERSIONS

LINUXBUILD=/lib/modules/`uname -r`/build/

obj-m   := si8821.o


# add a line (uncommented) like the following if multiple
# objects are needed to build the module
si8821-objs := module.o irup.o uart.o mmap.o ioctl.o

PWD		:= $(shell pwd)

all:
	make -C $(LINUXBUILD) SUBDIRS=$(PWD) modules

clean:
	make -C $(LINUXBUILD) SUBDIRS=$(PWD) clean

