# SPDX-License-Identifier: GPL-3.0-only
# MINIX v1 on the root block device.
fs-obj-$(CONFIG_ENABLE_FS_MINIX) += \
	fs/minix/minix_fs.o \
	fs/minix/minix_inode.o \
	fs/minix/minix_dir.o \
	fs/minix/minix_file.o \
	fs/minix/minix_meta.o
fs-type-$(CONFIG_ENABLE_FS_MINIX) += minix_fs_type
