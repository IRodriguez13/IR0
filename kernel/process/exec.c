/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exec.c
 * Description: Exec-path helpers: close FD_CLOEXEC fds before image replace.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

void process_exec_close_cloexec(process_t *p)
{
	int i;

	if (!p)
		return;

	for (i = 3; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &p->fd_table[i];

		if (!e->in_use)
			continue;
		if (!(e->fd_flags & FD_CLOEXEC))
			continue;
		if (e->is_pipe && e->vfs_file)
		{
			pipe_t *pip = (pipe_t *)e->vfs_file;

			pipe_fase49_fd_trace((uint32_t)p->task.pid, i, pip, e->pipe_end,
					     pip->fd_refs, "EXEC_CLOSE");
		}
		(void)process_close_fd(p, i);
	}
}

