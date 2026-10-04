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

int exec_detach_shared_mm(process_t *proc)
{
	mm_struct_t *old;
	mm_struct_t *fresh;
	address_space_root_t root;

	if (!proc || !proc->mm)
		return -EINVAL;
	if (mm_users(proc->mm) <= 1)
		return 0;

	fresh = mm_create();
	if (!fresh)
		return -ENOMEM;

	root = (address_space_root_t)create_process_page_directory();
	if (!root)
	{
		mm_put(fresh);
		return -ENOMEM;
	}

	mm_init_root(fresh, root, 1);
	old = proc->mm;
	/*
	 * Linux exec_mmap: bind + activate the private mm, then
	 * complete_vfork_done, then mmput(old). switch_to_user() must
	 * load the new root so a vfork child does not fetch the new
	 * image against the parent's tables.
	 */
	process_mm_bind(proc, fresh);
	process_set_mm_root(proc, (uint64_t)(uintptr_t)root);
	if (proc == current_process)
		mm_activate((uintptr_t)root);
	process_vfork_complete(proc);
	mm_put(old);
	return 0;
}

void process_exec_close_cloexec(process_t *p)
{
	fd_entry_t *table;
	int i;

	if (!p)
		return;

	table = process_fd_table(p);
	if (!table)
		return;

	for (i = 3; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &table[i];

		if (!e->in_use)
			continue;
		if (!(e->fd_flags & FD_CLOEXEC))
			continue;
		(void)process_close_fd(p, i);
	}
}
