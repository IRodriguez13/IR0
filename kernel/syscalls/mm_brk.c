/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_brk.c
 * Description: brk(2) wrapper. The program break is implemented by mm_brk().
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mm_syscalls.h"
#include <mm/brk.h>

int64_t sys_brk(void *addr)
{
	return mm_brk(addr);
}

/* sbrk is typically implemented as a userspace library function using brk */
/* POSIX does not require sbrk as a syscall */
