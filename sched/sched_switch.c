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
#include <ir0/context.h>
#include <ir0/oops.h>
#include <ir0/signals.h>
#include <stdint.h>

static inline uint64_t sched_switch_irq_save(void)
{
	return (uint64_t)arch_irq_save();
}

static inline void sched_switch_irq_restore(uint64_t flags)
{
	arch_irq_restore((unsigned long)flags);
}

void sched_context_switch_to(process_t *next)
{
	static int first = 1;
	process_t *prev;
	uint64_t irq_flags;
	int should_handle_signals = 0;

	if (!next)
		return;

	irq_flags = sched_switch_irq_save();
	prev = current_process;

	if (!first && prev == next)
	{
		sched_switch_irq_restore(irq_flags);
		return;
	}

	if (prev && prev->state == PROCESS_RUNNING)
	{
		should_handle_signals = 1;
		prev->state = PROCESS_READY;
	}

	if (should_handle_signals)
		handle_signals();

	next->state = PROCESS_RUNNING;
	current_process = next;

	if (signals_should_handle_on_run(next))
		handle_signals();

	if (first)
	{
		first = 0;
		arch_set_current_kernel_stack(next);
		arch_first_context_switch(next);
		panic("Returned from first context switch");
	}

	if (prev && next)
		arch_context_switch(&prev->task, &next->task);

	sched_switch_irq_restore(irq_flags);
}
