/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_introspect.c
 * Description: Process list / fd-table introspection (single implementation).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process_introspect.h>

#include <ir0/fd_types.h>
#include "process.h"

extern process_t *process_list;

int ir0_proc_snapshot_collect(ir0_proc_snapshot_t *out)
{
	process_t *p;
	uint64_t n = 0;
	uint64_t z = 0;
	uint64_t fds = 0;
	uint64_t pipes = 0;
	int i;

	if (!out)
		return -1;

	for (p = process_list; p; p = p->next)
	{
		fd_entry_t *fdt;

		n++;
		if (p->state == PROCESS_ZOMBIE)
			z++;
		fdt = process_fd_table(p);
		if (!fdt)
			continue;
		for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
		{
			if (!fdt[i].in_use)
				continue;
			fds++;
			if (fdt[i].is_pipe)
				pipes++;
		}
	}

	out->processes = n;
	out->zombies = z;
	out->open_fds = fds;
	out->pipes = pipes;
	return 0;
}

int32_t ir0_proc_current_pid(void)
{
	process_t *cur = process_get_current();

	return cur ? (int32_t)cur->task.pid : 0;
}
