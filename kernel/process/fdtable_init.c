/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fdtable_init.c
 * Description: Initial process descriptor table and standard streams.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <string.h>

void process_init_fd_table(process_t *process)
{
	fd_entry_t *table;
	int i;

	if (!process)
		return;
	if (!process->files)
	{
		process->files = files_create();
		if (!process->files)
			return;
	}
	table = process->files->fd_table;
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
		memset(&table[i], 0, sizeof(table[i]));
	table[0].in_use = true;
	strncpy(table[0].path, "/dev/stdin", sizeof(table[0].path) - 1);
	table[1].in_use = true;
	strncpy(table[1].path, "/dev/stdout", sizeof(table[1].path) - 1);
	table[2].in_use = true;
	strncpy(table[2].path, "/dev/stderr", sizeof(table[2].path) - 1);
}
