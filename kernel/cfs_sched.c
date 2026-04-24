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
