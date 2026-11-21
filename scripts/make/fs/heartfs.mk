# SPDX-License-Identifier: GPL-3.0-only
# heartfs: /heart (kernel meta, /heart/src, views of /proc and /sys). It has
# no Kconfig symbol; the kernel mounts it at boot, so it is always built.
fs-obj-y += fs/heartfs/heartfs.o
fs-type-y += heartfs_fs_type
