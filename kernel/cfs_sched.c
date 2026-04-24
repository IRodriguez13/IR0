/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cfs_sched.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - CFS scheduler compatibility backend
 *
 * Current implementation aliases round-robin scheduler behavior while keeping
 * a stable API surface for future CFS implementation.
 */

#include "cfs_sched.h"
#include "rr_sched.h"

void cfs_add_process(process_t *proc)
{
	rr_add_process(proc);
}

void cfs_remove_process(process_t *proc)
{
	rr_remove_process(proc);
}

void cfs_schedule_next(void)
{
	rr_schedule_next();
}
