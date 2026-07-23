/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fd_dispatch.h
 * Description: Shared fd_entry_t kind predicates for syscall dispatch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/process.h>
#include <stddef.h>

static inline int fd_entry_live(const fd_entry_t *e)
{
	return e && e->in_use;
}

static inline int fd_entry_pseudo(const fd_entry_t *e)
{
	return fd_entry_live(e) && e->is_pseudo && e->vfs_file != NULL;
}

static inline int fd_entry_pipe(const fd_entry_t *e)
{
	return fd_entry_live(e) && e->is_pipe;
}

static inline int fd_entry_socket(const fd_entry_t *e)
{
	return fd_entry_live(e) && e->is_socket && e->vfs_file != NULL;
}

static inline fd_entry_t *fd_table_slot(fd_entry_t *table, int fd)
{
	if (!table || fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return NULL;
	return &table[fd];
}
