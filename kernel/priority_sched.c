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
