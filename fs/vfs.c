/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.c
 * Description: VFS core — fs-op serialization, backend path
 *              translation and the path helpers the vfs_*.c units
 *              share.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/permissions.h>
#include <ir0/credentials.h>
#include <ir0/cpu.h>
#include <ir0/ipc.h>
#include <ir0/problems.h>
#include <ir0/sched.h>
#include <ir0/process.h>
#include <string.h>
#include "vfs_internal.h"

/*
 * One fs_ops critical section at a time: a sleeping lock (Linux mutex).
 * A contender queues itself BLOCKED and sleeps in schedule(); release
 * wakes the queue. IRQs stay enabled so ATA PIO still uses its own
 * irq_save per command.
 *
 * Recursive: vfs_open -> vfs_truncate, vfs_utimens -> vfs_stat,
 * vfs_read_file -> ops->stat/read, same task.
 *
 * Before the first task exists there is a single boot context, which is
 * the owner of any busy lock; contention without a current task means the
 * holder can never run again, so it is a bug, not a wait.
 */
static int vfs_fs_op_busy;
static int vfs_fs_op_depth;
static const void *vfs_fs_op_owner;
static wait_queue_t vfs_fs_op_waiters;

static const void *vfs_fs_op_self(void)
{
	if (current_process)
		return current_process;
	return &vfs_fs_op_busy;
}

void vfs_fs_op_acquire(void)
{
	const void *me = vfs_fs_op_self();

	for (;;)
	{
		unsigned long flags = irq_save();

		if (vfs_fs_op_busy && vfs_fs_op_owner == me)
		{
			vfs_fs_op_depth++;
			irq_restore(flags);
			return;
		}
		if (!vfs_fs_op_busy)
		{
			vfs_fs_op_busy = 1;
			vfs_fs_op_owner = me;
			vfs_fs_op_depth = 1;
			irq_restore(flags);
			return;
		}
		BUG_ON(!current_process);
		/* Queued and BLOCKED under the mask: release cannot slip by. */
		wait_queue_add(&vfs_fs_op_waiters, current_process);
		irq_restore(flags);
		if (current_process->state == PROCESS_BLOCKED)
			schedule();
	}
}

void vfs_fs_op_release(void)
{
	unsigned long flags = irq_save();

	if (vfs_fs_op_depth > 1)
	{
		vfs_fs_op_depth--;
		irq_restore(flags);
		return;
	}
	vfs_fs_op_busy = 0;
	vfs_fs_op_owner = NULL;
	vfs_fs_op_depth = 0;
	irq_restore(flags);
	wait_queue_wake_all(&vfs_fs_op_waiters);
}

int vfs_ops_stat(struct vfs_ops *ops, const char *path, stat_t *st)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->stat(backend, st);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_read(struct vfs_ops *ops, const char *path, void *buf,
		 size_t count, size_t *done, off_t offset)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->read(backend, buf, count, done, offset);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_write(struct vfs_ops *ops, const char *path, const void *buf,
		  size_t count, size_t *done, off_t offset)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->write(backend, buf, count, done, offset);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_create(struct vfs_ops *ops, const char *path, mode_t mode)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->create(backend, mode);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_mkdir(struct vfs_ops *ops, const char *path, mode_t mode)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->mkdir(backend, mode);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_unlink(struct vfs_ops *ops, const char *path)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->unlink(backend);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_rmdir(struct vfs_ops *ops, const char *path)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->rmdir(backend);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_readdir(struct vfs_ops *ops, const char *path,
		    struct vfs_dirent *entries, int max)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->readdir(backend, entries, max);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_link(struct vfs_ops *ops, const char *oldpath,
		 const char *newpath)
{
	char old_backend[VFS_PATH_MAX];
	char new_backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, oldpath, old_backend, sizeof(old_backend));
	if (ret != 0)
		return ret;
	ret = vfs_backend_path(ops, newpath, new_backend, sizeof(new_backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->link(old_backend, new_backend);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_symlink(struct vfs_ops *ops, const char *target,
		    const char *linkpath)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, linkpath, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->symlink(target, backend);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_readlink(struct vfs_ops *ops, const char *path, char *buf,
		     size_t buflen)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->readlink(backend, buf, buflen);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_rename(struct vfs_ops *ops, const char *oldpath,
		   const char *newpath)
{
	char old_backend[VFS_PATH_MAX];
	char new_backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, oldpath, old_backend, sizeof(old_backend));
	if (ret != 0)
		return ret;
	ret = vfs_backend_path(ops, newpath, new_backend, sizeof(new_backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->rename(old_backend, new_backend);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_chown(struct vfs_ops *ops, const char *path, uid_t owner,
		  gid_t group)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->chown(backend, owner, group);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_chmod(struct vfs_ops *ops, const char *path, mode_t mode)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->chmod(backend, mode);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_truncate(struct vfs_ops *ops, const char *path, size_t length)
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->truncate(backend, length);
	vfs_fs_op_release();
	return ret;
}

int vfs_ops_utimens(struct vfs_ops *ops, const char *path,
		    const struct timespec times[2])
{
	char backend[VFS_PATH_MAX];
	int ret;

	ret = vfs_backend_path(ops, path, backend, sizeof(backend));
	if (ret != 0)
		return ret;
	vfs_fs_op_acquire();
	ret = ops->utimens(backend, times);
	vfs_fs_op_release();
	return ret;
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

int vfs_validate_path(const char *path)
{
    if (!path)
        return -EINVAL;
    size_t len = strlen(path);
    if (len == 0)
        return -EINVAL;
    if (len >= MAX_PATH)
        return -ENAMETOOLONG;

    int comp = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            comp = 0;
        else if (++comp > 255)
            return -ENAMETOOLONG;

        if (*p < 0x20 || *p == 0x7F ||
            *p == '\\' || *p == '|' || *p == '<' || *p == '>')
            return -EINVAL;
    }
    return 0;
}

int vfs_backend_path(struct vfs_ops *ops, const char *path,
		     char *backend, size_t backend_size)
{
    struct vfs_mount *m;
    const char *suffix;
    size_t base_len;
    size_t suffix_len;

    if (!ops || !path || !backend || backend_size == 0)
        return -EINVAL;
    m = vfs_find_mount(path);
    if (!m || !m->fs || m->fs->ops != ops)
        return -ENODEV;

    if (strcmp(m->path, "/") == 0)
        suffix = path;
    else
        suffix = path + strlen(m->path);

    base_len = strlen(m->backend_path);
    suffix_len = strlen(suffix);
    if (strcmp(m->backend_path, "/") == 0)
    {
        if (suffix[0] == '\0')
            suffix = "/";
        else if (suffix[0] == '/')
            suffix++;
        suffix_len = strlen(suffix);
        if (suffix_len + 2 > backend_size)
            return -ENAMETOOLONG;
        backend[0] = '/';
        memcpy(backend + 1, suffix, suffix_len + 1);
        return 0;
    }

    if (base_len + suffix_len + 1 > backend_size)
        return -ENAMETOOLONG;
    memcpy(backend, m->backend_path, base_len);
    memcpy(backend + base_len, suffix, suffix_len + 1);
    return 0;
}

/**
 * Return the vfs_ops for the filesystem that owns @path, or NULL.
 */
struct vfs_ops *vfs_ops_for_path(const char *path)
{
    struct vfs_mount *m = vfs_find_mount(path);
    if (m && m->fs && m->fs->ops)
        return m->fs->ops;
    return NULL;
}

/**
 * Check that the current process has execute on every directory component.
 */
int vfs_check_dir_traverse(const char *path)
{
    if (!ir0_current_cred())
        return -EINVAL;
    if (ir0_cred_is_root())
        return 0;
    if (strcmp(path, "/") == 0)
        return 0;

    char dir[MAX_PATH];
    strncpy(dir, "/", sizeof(dir));
    const char *p = path + 1;

    while (*p) {
        const char *slash = strchr(p, '/');
        if (!slash)
            break;
        size_t clen = (size_t)(slash - p);
        size_t dlen = strlen(dir);
        if (dlen + 1 + clen >= sizeof(dir))
            return -ENAMETOOLONG;
        if (dlen > 1)
            strncat(dir, "/", sizeof(dir) - strlen(dir) - 1);
        strncat(dir, p, clen);
        dir[sizeof(dir) - 1] = '\0';

        if (!ir0_check_file_access(dir, ACCESS_EXEC))
            return -EACCES;
        p = slash + 1;
    }
    return 0;
}

/**
 * Convenience: extract parent directory of @path into @out.
 * Returns 0 on success.
 */
int vfs_parent_dir(const char *path, char *out, size_t out_sz)
{
    const char *last = strrchr(path, '/');
    if (!last || last == path) {
        strncpy(out, "/", out_sz);
        out[out_sz - 1] = '\0';
        return 0;
    }
    size_t len = (size_t)(last - path);
    if (len >= out_sz)
        return -ENAMETOOLONG;
    strncpy(out, path, len);
    out[len] = '\0';
    return 0;
}

/**
 * Look up the directory that will hold the new name @path, as Linux does
 * before it checks permissions: a missing parent is ENOENT and a parent
 * that is not a directory is ENOTDIR. The parent path is left in @out.
 */
int vfs_lookup_parent(const char *path, char *out, size_t out_sz)
{
    stat_t st;
    int ret = vfs_parent_dir(path, out, out_sz);

    if (ret != 0)
        return ret;
    ret = vfs_stat(out, &st);
    if (ret != 0)
        return ret;
    if (!S_ISDIR(st.st_mode))
        return -ENOTDIR;
    return 0;
}
