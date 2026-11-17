# SPDX-License-Identifier: GPL-3.0-only
# tmpfs (also mounted as ramfs): in-memory files, the fallback root.
fs-obj-$(CONFIG_ENABLE_FS_TMPFS) += fs/tmpfs/tmpfs.o
fs-type-$(CONFIG_ENABLE_FS_TMPFS) += tmpfs_fs_type
