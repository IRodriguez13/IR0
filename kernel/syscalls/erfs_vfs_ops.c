/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_vfs_ops.c
 * Description: vfs_dispatch bridge stat/read for the staged early rootfs.
 */

#include "erfs_internal.h"

static struct vfs_ops g_early_vfs_ops;

static int early_vfs_ops_path_backed(enum ir0_early_rootfs_node node)
{
	return node == IR0_EARLY_ROOTFS_INIT ||
	       node == IR0_EARLY_ROOTFS_ZERO ||
	       node == IR0_EARLY_ROOTFS_NULL ||
	       node == IR0_EARLY_ROOTFS_CONSOLE ||
	       node == IR0_EARLY_ROOTFS_TTY ||
	       node == IR0_EARLY_ROOTFS_FILE;
}

static int early_vfs_ops_stat(const char *path, stat_t *st)
{
	enum ir0_early_rootfs_node node;
	struct ir0_early_stat64 raw;
	int fsid;

	if (!path || !st)
		return -EFAULT;
	node = ir0_early_rootfs_path_node(path);
	if (node != IR0_EARLY_ROOTFS_NONE)
	{
		ir0_early_rootfs_fill_stat(node, g_blob_len, &raw);
		st->st_dev = (dev_t)raw.st_dev;
		st->st_ino = (ino_t)raw.st_ino;
		st->st_mode = raw.st_mode;
		st->st_nlink = raw.st_nlink;
		st->st_uid = (uid_t)raw.st_uid;
		st->st_gid = (gid_t)raw.st_gid;
		st->st_rdev = (dev_t)raw.st_rdev;
		st->st_size = raw.st_size;
		st->st_blksize = raw.st_blksize;
		st->st_blocks = raw.st_blocks;
		st->st_atime = raw.st_atime_sec;
		st->st_mtime = raw.st_mtime_sec;
		st->st_ctime = raw.st_ctime_sec;
		return 0;
	}
	fsid = ramtab_find(path);
	if (fsid < 0)
		return -ENOENT;
	st->st_dev = 1;
	st->st_ino = (ino_t)(16 + fsid);
	st->st_mode = ramtab_table()[fsid].mode;
	st->st_nlink = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size = (off_t)ramtab_table()[fsid].size;
	st->st_blksize = 4096;
	st->st_blocks = (blkcnt_t)((ramtab_table()[fsid].size + 511U) / 512U);
	st->st_atime = 0;
	st->st_mtime = 0;
	st->st_ctime = 0;
	return 0;
}

static int early_vfs_ops_read(const char *path, void *buf, size_t count,
			      size_t *bytes_read, off_t offset)
{
	enum ir0_early_rootfs_node node;
	uint8_t *dst = buf;
	size_t n;
	size_t i;
	int fsid;

	if (!bytes_read)
		return -EFAULT;
	*bytes_read = 0;
	if (offset < 0)
		return -EINVAL;
	if (count == 0)
		return 0;
	if (!buf)
		return -EFAULT;
	node = ir0_early_rootfs_path_node(path);
	if (node == IR0_EARLY_ROOTFS_ROOT ||
	    node == IR0_EARLY_ROOTFS_DEV ||
	    node == IR0_EARLY_ROOTFS_TMP)
		return -EISDIR;
	if (node == IR0_EARLY_ROOTFS_CONSOLE ||
	    node == IR0_EARLY_ROOTFS_TTY ||
	    node == IR0_EARLY_ROOTFS_NULL)
		return 0;
	if (node == IR0_EARLY_ROOTFS_ZERO)
	{
		n = count;
		for (i = 0; i < n; i++)
			dst[i] = 0;
		*bytes_read = n;
		return 0;
	}
	if (node == IR0_EARLY_ROOTFS_INIT)
	{
		if (!g_blob || (uint64_t)offset >= g_init_vis)
			return 0;
		n = (size_t)(g_init_vis - (uint64_t)offset);
		if (n > count)
			n = count;
		if ((uint64_t)offset >= g_blob_len)
			return 0;
		if (n > (size_t)(g_blob_len - (uint64_t)offset))
			n = (size_t)(g_blob_len - (uint64_t)offset);
		for (i = 0; i < n; i++)
			dst[i] = g_blob[(uint64_t)offset + i];
		*bytes_read = n;
		return 0;
	}
	fsid = ramtab_find(path);
	if (fsid < 0)
		return -ENOENT;
	if ((ramtab_table()[fsid].mode & IR0_EARLY_ROOTFS_S_IFMT) ==
	    IR0_EARLY_ROOTFS_S_IFDIR)
		return -EISDIR;
	if ((uint64_t)offset >= ramtab_table()[fsid].size)
		return 0;
	n = (size_t)(ramtab_table()[fsid].size - (uint64_t)offset);
	if (n > count)
		n = count;
	for (i = 0; i < n; i++)
		dst[i] = ramtab_table()[fsid].data ? ramtab_table()[fsid].data[(uint64_t)offset + i] :
					   0;
	*bytes_read = n;
	return 0;
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

void early_rootfs_bind_vfs_ops(void)
{
	g_vfs_ops_used = 0;
	g_early_vfs_ops.stat = early_vfs_ops_stat;
	g_early_vfs_ops.read = early_vfs_ops_read;
	(void)vfs_dispatch_bind(&g_early_vfs_ops);
}

int early_rootfs_vfs_ops_used(void)
{
	return g_vfs_ops_used;
}
