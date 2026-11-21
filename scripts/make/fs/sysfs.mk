# SPDX-License-Identifier: GPL-3.0-only
# sysfs: /sys. No Kconfig symbol; the kernel mounts it at boot.
fs-obj-y += fs/sysfs/sysfs.o
fs-type-y += sysfs_fs_type
