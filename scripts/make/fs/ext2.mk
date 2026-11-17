# SPDX-License-Identifier: GPL-3.0-only
# ext2 on a block device: the ext2 product root and ext2 mounts.
fs-obj-$(CONFIG_ENABLE_FS_EXT2) += fs/ext2/ext2_disk.o
fs-type-$(CONFIG_ENABLE_FS_EXT2) += ext2_fs_type
