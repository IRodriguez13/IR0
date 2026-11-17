# SPDX-License-Identifier: GPL-3.0-only
# simplefs: a minimal in-memory filesystem with a flat store per source.
fs-obj-$(CONFIG_ENABLE_FS_SIMPLEFS) += fs/simplefs/simplefs.o
fs-type-$(CONFIG_ENABLE_FS_SIMPLEFS) += simplefs_fs_type
