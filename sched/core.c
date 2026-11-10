/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: core.c
 * Description: Scheduler core: pick the next task, switch to it, reschedule.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "core.h"
#include <ir0/clock.h>
#include <ir0/context.h>
#include <ir0/oops.h>
#include <ir0/percpu.h>
#include <ir0/process.h>

static int sched_has_running_context;

void sched_switch_to(process_t *next)
{
	process_t *prev;
	unsigned long irq_flags;

	if (!next)
		return;

	irq_flags = rq_lock();
	prev = current_process;

	if (sched_has_running_context && prev == next)
	{
		rq_unlock(irq_flags);
		return;
	}

	if (prev && prev->state == PROCESS_RUNNING)
		process_set_sched_state(prev, PROCESS_READY);

	process_set_sched_state(next, PROCESS_RUNNING);
	process_set_current(next);
	cpu_local_note_context_switch();

	if (!sched_has_running_context)
	{
		sched_has_running_context = 1;
		switch_to(NULL, &next->task);
		panic("Returned from first context switch");
	}

	if (prev && next)
	{
		clock_note_context_switch();
		switch_to(&prev->task, &next->task);
	}

	rq_unlock(irq_flags);
}

void schedule(void)
{
	process_t *next = sched_pick_next();

	if (next)
		sched_switch_to(next);
}

int sched_take_resched(void)
{
	if (!current_process || current_process->state == PROCESS_BLOCKED)
		return 0;
	if (sched_count_runnable() <= 1)
		return 0;
	if (!clock_take_sched_resched_pending())
		return 0;
	return 1;
}
