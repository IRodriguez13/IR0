/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: hostshare_9p.c
 * Description: VFS fstype "9p" backed by virtio-9p (QEMU -virtfs host share).
 */

#include "vfs.h"
#include <ir0/errno.h>
#include <ir0/virtio_9p.h>
#include <string.h>
#include <stddef.h>

static char g_mount_point[VFS_PATH_MAX];
static int g_mounted;

static const char *rel_of(const char *path)
{
	size_t mlen;

	if (!path || !g_mounted)
		return NULL;
	mlen = strlen(g_mount_point);
	if (strncmp(path, g_mount_point, mlen) != 0)
		return NULL;
	if (path[mlen] == '\0')
		return "";
	if (path[mlen] != '/')
		return NULL;
	return path + mlen + 1;
}

static int hs_stat(const char *path, stat_t *buf)
{
	char tmp[256];
	int n;
	const char *rel = rel_of(path);

	if (!buf)
		return -EFAULT;
	if (!rel)
		return -ENOENT;
	if (rel[0] == '\0')
	{
		memset(buf, 0, sizeof(*buf));
		buf->st_mode = S_IFDIR | 0755;
		return 0;
	}
	n = virtio_9p_read_file(rel, tmp, sizeof(tmp));
	if (n < 0)
		return n;
	memset(buf, 0, sizeof(*buf));
	buf->st_mode = S_IFREG | 0644;
	buf->st_size = (off_t)n;
	return 0;
}

static int hs_create(const char *path, mode_t mode)
{
	const char *rel = rel_of(path);
	(void)mode;
	if (!rel || !rel[0])
		return -EINVAL;
	return virtio_9p_write_file(rel, "", 0);
}

static int hs_read(const char *path, void *buf, size_t count, size_t *bytes_read,
		   off_t offset)
{
	char tmp[4096];
	int n;
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EISDIR;
	if (offset < 0)
		return -EINVAL;
	n = virtio_9p_read_file(rel, tmp, sizeof(tmp));
	if (n < 0)
		return n;
	if (offset >= n)
	{
		if (bytes_read)
			*bytes_read = 0;
		return 0;
	}
	if ((size_t)(n - offset) < count)
		count = (size_t)(n - offset);
	memcpy(buf, tmp + offset, count);
	if (bytes_read)
		*bytes_read = count;
	return 0;
}

static int hs_write(const char *path, const void *buf, size_t count,
		    size_t *bytes_written, off_t offset)
{
	const char *rel = rel_of(path);
	int rc;

	if (!rel || !rel[0])
		return -EISDIR;
	if (offset != 0)
		return -ENOSYS; /* MVP: whole-file replace */
	rc = virtio_9p_write_file(rel, buf, count);
	if (rc < 0)
		return rc;
	if (bytes_written)
		*bytes_written = count;
	return 0;
}

static int hs_truncate(const char *path, size_t length)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EISDIR;
	if (length != 0)
		return -ENOSYS; /* MVP: O_TRUNC → empty only */
	return virtio_9p_write_file(rel, "", 0);
}

static int hs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
	(void)path;
	(void)entries;
	(void)max;
	return 0;
}

static struct vfs_ops hs_ops = {
	.stat = hs_stat,
	.create = hs_create,
	.read = hs_read,
	.write = hs_write,
	.truncate = hs_truncate,
	.readdir = hs_readdir,
};

static int hs_mount(const char *dev, const char *dir)
{
	(void)dev;
	if (!dir || !virtio_9p_ready())
		return -ENODEV;
	strncpy(g_mount_point, dir, sizeof(g_mount_point) - 1);
	g_mount_point[sizeof(g_mount_point) - 1] = '\0';
	g_mounted = 1;
	return 0;
}

static int hs_umount(const char *dir)
{
	(void)dir;
	g_mounted = 0;
	g_mount_point[0] = '\0';
	return 0;
}

static struct vfs_fstype hs_fstype = {
	.name = "9p",
	.ops = &hs_ops,
	.mount = hs_mount,
	.umount = hs_umount,
	.next = NULL,
};

int hostshare_9p_register(void)
{
	return vfs_register_fs(&hs_fstype);
}
