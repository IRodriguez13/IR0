/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: priority_sched.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Priority scheduler compatibility backend
 *
 * Current implementation aliases round-robin scheduler behavior while keeping
 * a stable API surface for future priority scheduler implementation.
 */

#include "priority_sched.h"
#include "rr_sched.h"

void priority_add_process(process_t *proc)
{
	rr_add_process(proc);
}

void priority_remove_process(process_t *proc)
{
	rr_remove_process(proc);
}

void priority_schedule_next(void)
{
	rr_schedule_next();
}
