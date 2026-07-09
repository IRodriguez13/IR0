/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: host_fase48_stub.c
 * Description: No-op fase48 FD stats stubs for host-linked pipe.c
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

void fase48_note_fd_created(void)
{
}

void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
			 uint64_t *blocked_readers, uint64_t *blocked_writers)
{
	if (created)
		*created = 0;
	if (destroyed)
		*destroyed = 0;
	if (blocked_readers)
		*blocked_readers = 0;
	if (blocked_writers)
		*blocked_writers = 0;
}
