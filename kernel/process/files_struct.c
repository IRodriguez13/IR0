/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: files_struct.c
 * Description: files_struct refcount and process bind/share/clone helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/files_struct.h>
#include <ir0/fd_resource.h>
#include <ir0/errno.h>
#include <string.h>

static int process_files_acquire_entries(files_struct_t *f)
{
	int i;

	if (!f)
		return -EINVAL;

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &f->fd_table[i];

		if (fd_resource_acquire(e) != 0)
		{
			klog_error_fmt("FD", "fork clone rejected fd=%d path=%s provider=%s\n",
				       i, e->path[0] ? e->path : "(anonymous)",
				       e->resource_ops ? e->resource_ops->name : "(missing)");
			while (--i >= 0)
				(void)fd_resource_release(&f->fd_table[i]);
			return -EINVAL;
		}
	}
	return 0;
}

int process_files_share(process_t *child, process_t *parent)
{
	files_struct_t *f;

	if (!child || !parent)
		return -EINVAL;

	f = parent->files;
	if (!files_struct_live(f))
		return -EINVAL;

	if (!files_get(f))
		return -EINVAL;
	process_files_bind(child, f);
	return 0;
}

int process_files_clone(process_t *child, process_t *parent)
{
	files_struct_t *nf;

	if (!child || !parent || !files_struct_live(parent->files))
		return -EINVAL;

	nf = files_create();
	if (!nf)
		return -ENOMEM;

	memcpy(nf->fd_table, parent->files->fd_table, sizeof(nf->fd_table));
	if (process_files_acquire_entries(nf) != 0)
	{
		files_put(nf);
		return -ENOMEM;
	}

	process_files_bind(child, nf);
	return 0;
}
