/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_vfs_ops.c
 * Description: vfs_dispatch bridge stat/read for the staged early rootfs.
 */

#include "erfs_internal.h"

static int early_vfs_ops_path_backed(enum ir0_early_rootfs_node node)
{
	return node == IR0_EARLY_ROOTFS_INIT ||
	       node == IR0_EARLY_ROOTFS_ZERO ||
	       node == IR0_EARLY_ROOTFS_NULL ||
	       node == IR0_EARLY_ROOTFS_CONSOLE ||
	       node == IR0_EARLY_ROOTFS_TTY ||
	       node == IR0_EARLY_ROOTFS_FILE;
}

int64_t early_try_vfs_ops_read(int fd, enum ir0_early_rootfs_node node,
			       uint64_t buffer, uint64_t count)
{
	struct vfs_file *f;
	uint8_t tmp[256];
	size_t n;
	int ret;

	if (!early_vfs_ops_path_backed(node))
		return -ENODEV;
	if (!vfs_dispatch_bound() || !g_fd_ops || !g_fd_ops->file)
		return -ENODEV;
	f = g_fd_ops->file(fd);
	if (!f || !f->path[0])
		return -ENODEV;
	n = count > sizeof(tmp) ? sizeof(tmp) : (size_t)count;
	ret = vfs_dispatch_read(f, tmp, n);
	if (ret < 0)
		return ret;
	if (ret == 0)
	{
		g_vfs_ops_used = 1;
		return 0;
	}
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, tmp, (uint64_t)ret) != 0)
		return -EFAULT;
	g_vfs_ops_used = 1;
	return (int64_t)ret;
}

