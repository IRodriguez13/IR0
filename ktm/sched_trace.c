/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched_trace.c
 * Description: ISA-neutral process scheduler transition instrumentation.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process.h>
#include <ir0/ktm/event.h>

void process_sched_state_trace(const process_t *p, process_state_t prev,
			       process_state_t next, void *caller)
{
	if (!p)
		return;

	/* The event pid is the waker; arg0 identifies the affected task. */
	ktm_event_emit4(next == PROCESS_BLOCKED ? KTM_EVENT_BLOCK
						: KTM_EVENT_WAKE,
			KTM_SUBSYS_SCHED,
			(uint64_t)(uint32_t)p->task.pid,
			(uint64_t)prev, (uint64_t)next,
			(uint64_t)(uintptr_t)caller);
}
