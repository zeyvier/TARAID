# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux tafs routines.
#

obj-m += tafs.o

tafs-m	:= balloc.o bitmap.o block_validity.o dir.o ext4_jbd2.o extents.o \
		extents_status.o file.o fsmap.o fsync.o hash.o ialloc.o \
		indirect.o inline.o inode.o ioctl.o mballoc.o migrate.o \
		mmp.o move_extent.o namei.o page-io.o readpage.o resize.o \
		super.o symlink.o sysfs.o xattr.o xattr_hurd.o xattr_trusted.o \
		xattr_user.o fast_commit.o orphan.o

tafs-$(CONFIG_EXT4_FS_POSIX_ACL)	+= acl.o
tafs-$(CONFIG_EXT4_FS_SECURITY)		+= xattr_security.o
tafs-$(CONFIG_FS_VERITY)		+= verity.o

all:
	make -j18 -C /lib/modules/$(shell uname -r)/build M=$(shell PWD)  modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell PWD) clean
