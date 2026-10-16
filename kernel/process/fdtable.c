/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fdtable.c
 * Description: Process fd_table init, release on exit/destroy, and fork duplicate.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <stddef.h>
#include <ir0/fd_get.h>
#include <ir0/fd_install.h>
#include <ir0/fd_resource.h>
#include <ir0/vfs_file.h>

int ir0_fd_get(process_t *proc, int fd, ir0_fd_t *out)
{
	files_struct_t *f;
	fd_entry_t *e;

	if (!out)
		return -EINVAL;

	out->files = NULL;
	out->entry = NULL;
	out->fd = -1;

	if (!proc)
		return -ESRCH;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;

	f = proc->files;
	if (!files_struct_live(f))
		return -EBADF;
	if (!files_get(f))
		return -EBADF;

	e = &f->fd_table[fd];
	if (!e->in_use)
	{
		files_put(f);
		return -EBADF;
	}

	out->files = f;
	out->entry = e;
	out->fd = fd;
	return 0;
}

void ir0_fd_put(ir0_fd_t *h)
{
	if (!h || !h->files)
		return;
	files_put(h->files);
	h->files = NULL;
	h->entry = NULL;
	h->fd = -1;
}

/*
 * Close one descriptor owned by @proc without depending on the syscall
 * dispatcher.  exec uses this for FD_CLOEXEC; sys_close adds only the
 * userspace accounting around the same lifecycle operation.
 */
int64_t process_close_fd(process_t *proc, int fd)
{
	ir0_fd_t h;
	fd_entry_t *entry;
	int ret;

	ret = ir0_fd_get(proc, fd, &h);
	if (ret != 0)
		return ret;
	entry = h.entry;

	/* Bare console stdio has no backing resource to release. */
	if (fd <= 2 && !entry->resource_ops && !entry->vfs_file)
	{
		memset(entry, 0, sizeof(*entry));
		entry->pipe_end = -1;
		ir0_fd_put(&h);
		return 0;
	}

	ret = fd_resource_release(entry);
	if (ret == 0)
	{
		memset(entry, 0, sizeof(*entry));
		entry->pipe_end = -1;
	}
	ir0_fd_put(&h);
	return ret;
}

void process_release_fds(process_t *p, const char *pipe_trace_op)
{
	fd_entry_t *table;
	int i;

	(void)pipe_trace_op;

	if (!p || !files_struct_live(p->files))
		return;

	/*
	 * Shared files_struct (CLONE_FILES): other processes still use the table;
	 * only the last reference closes underlying handles.
	 */
	if (p->files->refcount != 1)
		return;

	table = p->files->fd_table;
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &table[i];

		if (!e->in_use)
			continue;

		if (fd_resource_release(e) != 0)
			panic("process_release_fds: descriptor without lifecycle provider");
		/*
		 * Console stdio (0/1/2) with no backend: fall through and clear.
		 * Redirected stdio must take the branches above (Linux closes all).
		 */

		memset(e, 0, sizeof(*e));
		e->pipe_end = -1;
	}
}

int process_duplicate_fd_table(process_t *parent, process_t *child)
{
	return process_files_clone(child, parent);
}

static void fd_copy_path(char *dst, size_t dst_size, const char *src)
{
	size_t i;

	if (!dst || dst_size == 0)
		return;
	if (!src)
	{
		dst[0] = '\0';
		return;
	}
	for (i = 0; src[i] != '\0' && i + 1 < dst_size; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

static int fd_install_current(int fd, const char *path, int flags)
{
	const struct ir0_vfs_file_ops *file_ops = ir0_vfs_file_product();
	fd_entry_t *e;
	struct vfs_file *f;

	if (!current_process || !files_struct_live(current_process->files))
		return -ESRCH;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;
	if (!file_ops || !file_ops->alloc)
		return -EINVAL;
	f = file_ops->alloc(path, flags);
	if (!f)
		return -ENOMEM;
	e = &current_process->files->fd_table[fd];
	if (e->in_use)
		(void)fd_resource_release(e);
	memset(e, 0, sizeof(*e));
	e->in_use = true;
	e->flags = flags;
	e->pipe_end = -1;
	e->vfs_file = f;
	e->resource_ops = ir0_vfs_file_resource_ops();
	fd_copy_path(e->path, sizeof(e->path), path);
	return 0;
}

static struct vfs_file *fd_file_current(int fd)
{
	ir0_fd_t h;
	struct vfs_file *f;

	if (ir0_fd_get(current_process, fd, &h) != 0)
		return NULL;
	f = (struct vfs_file *)h.entry->vfs_file;
	ir0_fd_put(&h);
	return f;
}

static int fd_check_current(int fd)
{
	ir0_fd_t h;
	int ret;

	ret = ir0_fd_get(current_process, fd, &h);
	if (ret != 0)
		return ret;
	ir0_fd_put(&h);
	return 0;
}

static int fd_close_current(int fd)
{
	return (int)process_close_fd(current_process, fd);
}

static int fd_replace_child_files(int64_t child_pid, int share)
{
	process_t *child;
	files_struct_t *old;
	int ret;

	child = process_find_by_pid((pid_t)child_pid);
	if (!child || !current_process)
		return -ESRCH;
	old = child->files;
	child->files = NULL;
	if (share)
		ret = process_files_share(child, current_process);
	else
		ret = process_files_clone(child, current_process);
	if (ret < 0)
	{
		child->files = old;
		return ret;
	}
	if (old && old != child->files)
		files_put(old);
	return 0;
}

static int fd_clone_files(int64_t child_pid)
{
	return fd_replace_child_files(child_pid, 0);
}

static int fd_share_files(int64_t child_pid)
{
	return fd_replace_child_files(child_pid, 1);
}

static const struct ir0_fd_install_ops g_fd_install_ops = {
	.install = fd_install_current,
	.check = fd_check_current,
	.close = fd_close_current,
	.clone_files = fd_clone_files,
	.share_files = fd_share_files,
	.file = fd_file_current,
};

const struct ir0_fd_install_ops *ir0_fd_install_product(void)
{
	return &g_fd_install_ops;
}
