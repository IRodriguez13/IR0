/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched_switch.c
 * Description: Shared context-switch path for scheduler backends.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "sched_switch.h"

#include <ir0/arch_port.h>
#include <ir0/clock.h>
#include <ir0/context.h>
#include <ir0/switch.h>
#include <ir0/oops.h>
#include <ir0/sched.h>
#include <stdint.h>

static int sched_has_running_context;

static inline uint64_t sched_switch_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void sched_switch_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

void sched_context_switch_to(process_t *next)
{
	process_t *prev;
	uint64_t irq_flags;

	if (!next)
		return;

	irq_flags = sched_switch_irq_save();
	prev = current_process;

	if (sched_has_running_context && prev == next)
	{
		sched_switch_irq_restore(irq_flags);
		return;
	}

	if (prev && prev->state == PROCESS_RUNNING)
		process_set_sched_state(prev, PROCESS_READY);

	process_set_sched_state(next, PROCESS_RUNNING);
	current_process = next;

	if (!sched_has_running_context)
	{
		sched_has_running_context = 1;
		set_current_kernel_stack(next);
		first_switch_to(next);
		panic("Returned from first context switch");
	}

	if (prev && next)
	{
		clock_note_context_switch();
		switch_to(&prev->task, &next->task);
	}

	sched_switch_irq_restore(irq_flags);
}

void sched_adopt_running_context(process_t *running)
{
	uint64_t irq_flags;

	irq_flags = sched_switch_irq_save();
	current_process = running;
	sched_has_running_context = running != NULL;
	if (running)
		process_set_sched_state(running, PROCESS_RUNNING);
	sched_switch_irq_restore(irq_flags);
}
