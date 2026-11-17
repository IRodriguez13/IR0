# SPDX-License-Identifier: GPL-3.0-only
# 9p: the QEMU -virtfs host share over the virtio-9p client. It has no
# Kconfig symbol; like the virtio-9p client it is always built.
fs-obj-y += fs/9p/hostshare_9p.o
fs-type-y += v9fs_fs_type
