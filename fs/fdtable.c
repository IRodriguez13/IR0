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

#include <ir0/process.h>
#include <ir0/files_struct.h>
#include <ir0/oops.h>
#include <string.h>
#include <ir0/fd_get.h>
#include <ir0/fd_install.h>
#include <ir0/fd_resource.h>
#include <ir0/vfs_file.h>
#include <ir0/errno.h>

int ir0_fd_span(void)
{
	if (!current_process)
		return 0;
	return process_fd_span(current_process);
}

int fd_alloc_lowest(int from)
{
	process_t *p = current_process;
	files_struct_t *f;
	int i;
	int span;
	int limit;
	int rc;

	if (!p || !files_struct_live(p->files))
		return -ESRCH;
	if (from < 0)
		from = 0;
	limit = process_nofile_limit(p);
	if (from >= limit)
		return -EMFILE;

	f = p->files;
	span = process_fd_span(p);
	for (i = from; i < span && i < limit; i++)
	{
		if (!f->fd_table[i].in_use)
			return i;
	}

	i = span > from ? span : from;
	if (i >= limit)
		return -EMFILE;
	rc = files_grow(f, i + 1, limit);
	if (rc < 0)
		return rc;
	return i;
}

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
	if (fd < 0 || fd >= process_fd_span(proc))
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
	for (i = 0; i < process_fd_span(p); i++)
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

