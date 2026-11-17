/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls_glue.h
 * Description: IR0 kernel header — syscalls glue
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/process.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/pipe.h>
#include <ir0/copy_user.h>
#include "io_syscalls.h"

/*
 * Lowest free descriptor at or above @from, or -1 when the table is full.
 *
 * open(2) starts at 0: POSIX gives every new descriptor the lowest free
 * number, so close(0) then open(file) yields fd 0. pipe(2) still starts at
 * 3, a divergence from that rule: an unredirected stdio descriptor is served
 * by the console backend with its table slot left free, and a pipe placed
 * in that slot would shadow the console. It goes away once fds 0-2 are
 * real table entries.
 */
static inline int fd_alloc_lowest(fd_entry_t *fd_table, int from)
{
	int i;

	if (!fd_table)
		return -1;
	for (i = (from < 0) ? 0 : from; i < MAX_FDS_PER_PROCESS; i++)
	{
		if (!fd_table[i].in_use)
			return i;
	}
	return -1;
}

/* Resolve user path against dirfd (Linux openat/fstatat subset). */
int ir0_resolve_path_at(int dirfd, const char *user_path, char *resolved,
                        size_t resolved_sz);

/* Unredirected stdin read path (keyboard wait + copy to user). */
int64_t syscalls_read_stdio_stdin(void *buf, size_t count);
