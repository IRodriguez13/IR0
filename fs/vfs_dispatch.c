/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_dispatch.c
 * Description: Generic-core vfs_ops registry and file-relative I/O.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/errno.h>
#include <ir0/vfs_dispatch.h>
#include <ir0/vfs_file.h>

static struct vfs_ops *g_ops;

int vfs_dispatch_bind(struct vfs_ops *ops)
{
	if (!ops || !ops->stat || !ops->read)
		return -EINVAL;
	g_ops = ops;
	return 0;
}

struct vfs_ops *vfs_dispatch_ops(const char *path)
{
	if (!path || path[0] != '/' || !g_ops)
		return NULL;
	return g_ops;
}

int vfs_dispatch_bound(void)
{
	return g_ops != NULL;
}

int vfs_dispatch_stat(const char *path, stat_t *st)
{
	struct vfs_ops *ops = vfs_dispatch_ops(path);

	if (!ops)
		return -ENODEV;
	if (!st)
		return -EFAULT;
	return ops->stat(path, st);
}

int vfs_dispatch_read(struct vfs_file *f, void *buf, size_t count)
{
	struct vfs_ops *ops;
	size_t done = 0;
	int ret;

	if (!f)
		return -EBADF;
	if (!buf && count)
		return -EFAULT;
	if (count == 0)
		return 0;
	if (f->pos < 0)
		return -EINVAL;
	ops = vfs_dispatch_ops(f->path);
	if (!ops)
		return -ENODEV;
	ret = ops->read(f->path, buf, count, &done, f->pos);
	if (ret != 0)
		return ret;
	f->pos += (off_t)done;
	return (int)done;
}
