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
#include <ir0/fd_get.h>
#include <ir0/fd_resource.h>

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
