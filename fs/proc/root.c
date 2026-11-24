/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: root.c
 * Description: The proc filesystem type (Linux fs/proc/root.c). /proc is an
 *              ordinary VFS mount: every path operation reaches procfs
 *              through these vfs_ops, so no syscall tests a path prefix.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fs/vfs.h>
#include <ir0/cred.h>
#include <ir0/errno.h>
#include <ir0/open_flags.h>
#include <ir0/procfs.h>
#include <ir0/pseudo_fs.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <string.h>

#define PROC_ROOT "/proc"

static int proc_is_dir(const char *path)
{
	stat_t st;

	return proc_stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int procfs_stat(const char *path, stat_t *st)
{
	return proc_stat(path, st);
}

/*
 * Directories with a fixed layout (the root, a pid, its fd/ and task/)
 * come from proc_readdir(); the others (net/, sys/kernel/) are the
 * prefixes of registered nodes.
 */
static int procfs_readdir(const char *path, struct vfs_dirent *entries,
			  int max)
{
	int n;

	n = proc_readdir(path, entries, max);
	if (n != -ENOTDIR && n != -ENOENT)
		return n;
	pseudo_fs_nodes_register_all();
	if (pseudo_fs_path_has_children(path))
		return pseudo_fs_collect_registry_children(path, entries, max, 0);
	return proc_stat(path, &(stat_t){ 0 }) == 0 ? -ENOTDIR : -ENOENT;
}

/* The fd and exe links of another user's task are not readable (Linux). */
static int procfs_readlink(const char *path, char *buf, size_t buflen)
{
	const struct ir0_task_cred *cred = ir0_current_cred();
	int ret;

	if (cred)
	{
		ret = proc_access_path(path, cred->euid);
		if (ret != 0)
			return ret;
	}
	return proc_readlink(path, buf, buflen);
}

/*
 * The node behind @path for one operation; release it with
 * pseudo_fs_release_ops(). Directories have none: they are EISDIR.
 */
static int procfs_acquire(const char *path, int flags,
			  const pseudo_fs_ops_t **ops, void **ctx, int *dynamic)
{
	if (proc_is_dir(path))
		return -EISDIR;
	pseudo_fs_nodes_register_all();
	return (int)pseudo_fs_acquire_path(path, flags, ops, ctx, dynamic);
}

/*
 * Linux proc_pid_permission() and friends: environ, mem-like files and
 * the fd links of another user's task are refused even where the mode
 * bits the VFS already checked would allow them.
 */
static int procfs_open(const char *path, int flags)
{
	const struct ir0_task_cred *cred = ir0_current_cred();
	const pseudo_fs_ops_t *ops;
	void *ctx;
	int dynamic;
	int ret;

	if (proc_is_dir(path))
		return (flags & IR0_O_DIRECTORY) ? 0 : -EISDIR;
	if (flags & IR0_O_DIRECTORY)
		return proc_stat(path, &(stat_t){ 0 }) == 0 ? -ENOTDIR : -ENOENT;
	if (cred)
	{
		ret = proc_access_path(path, cred->euid);
		if (ret != 0)
			return ret;
	}
	ret = procfs_acquire(path, flags, &ops, &ctx, &dynamic);
	if (ret != 0)
		return ret;
	return (int)pseudo_fs_release_ops(ops, ctx, dynamic);
}

static int procfs_read(const char *path, void *buf, size_t count,
		       size_t *bytes_read, off_t offset)
{
	const pseudo_fs_ops_t *ops;
	void *ctx;
	int dynamic;
	int64_t n;
	int ret;

	*bytes_read = 0;
	if (offset < 0)
		return -EINVAL;
	ret = procfs_acquire(path, IR0_O_RDONLY, &ops, &ctx, &dynamic);
	if (ret != 0)
		return ret;
	n = pseudo_fs_ops_read(ops, ctx, (char *)buf, count, &offset);
	(void)pseudo_fs_release_ops(ops, ctx, dynamic);
	if (n < 0)
		return (int)n;
	*bytes_read = (size_t)n;
	return 0;
}

/* proc files take a write as a whole, without an offset (Linux simple attrs). */
static int procfs_write(const char *path, const void *buf, size_t count,
			size_t *bytes_written, off_t offset)
{
	const pseudo_fs_ops_t *ops;
	void *ctx;
	int dynamic;
	int64_t n;
	int ret;

	(void)offset;
	*bytes_written = 0;
	ret = procfs_acquire(path, IR0_O_WRONLY, &ops, &ctx, &dynamic);
	if (ret != 0)
		return ret;
	n = pseudo_fs_ops_write(ops, ctx, (const char *)buf, count);
	(void)pseudo_fs_release_ops(ops, ctx, dynamic);
	if (n < 0)
		return (int)n;
	*bytes_written = (size_t)n;
	return 0;
}

/* Generated files have no stored size; O_TRUNC is accepted and changes nothing. */
static int procfs_truncate(const char *path, size_t length)
{
	stat_t st;
	int ret;

	(void)length;
	ret = proc_stat(path, &st);
	if (ret != 0)
		return ret;
	return S_ISDIR(st.st_mode) ? -EISDIR : 0;
}

/* Linux proc_root_lookup() has no create: a new name is EACCES. */
static int procfs_create(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	return -EACCES;
}

static struct vfs_ops procfs_ops = {
	.stat = procfs_stat,
	.create = procfs_create,
	.readdir = procfs_readdir,
	.readlink = procfs_readlink,
	.open = procfs_open,
	.read = procfs_read,
	.write = procfs_write,
	.truncate = procfs_truncate,
};

static int procfs_mount(const char *dev, const char *dir)
{
	(void)dev;
	if (!dir || strcmp(dir, PROC_ROOT) != 0)
		return -EINVAL;
	return 0;
}

static int procfs_umount(const char *dir)
{
	(void)dir;
	return -EBUSY;
}

static int procfs_statfs(const char *path, struct ir0_statfs *buf)
{
	(void)path;
	buf->f_type = IR0_PROC_SUPER_MAGIC;
	buf->f_bsize = 4096;
	buf->f_frsize = 4096;
	buf->f_namelen = 255;
	return 0;
}

struct vfs_fstype proc_fs_type = {
	.name = "proc",
	.fs_flags = FS_NOSUID_DEFAULT | FS_SINGLE_INSTANCE,
	.ops = &procfs_ops,
	.mount = procfs_mount,
	.umount = procfs_umount,
	.statfs = procfs_statfs,
};
