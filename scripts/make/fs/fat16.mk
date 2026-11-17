# SPDX-License-Identifier: GPL-3.0-only
# FAT16 on a block device or one of its partitions.
fs-obj-$(CONFIG_ENABLE_FS_FAT16) += \
	fs/fat16/fat16_fs.o \
	fs/fat16/fat16_disk.o
fs-type-$(CONFIG_ENABLE_FS_FAT16) += fat16_fs_type
