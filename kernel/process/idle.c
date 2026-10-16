/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Portable idle loop. Owners bind through includes/ir0/idle.h.
 */

#include <ir0/arch_io.h>
#include <ir0/idle.h>
#include <ir0/irq.h>
#include <ir0/sched.h>

static struct process_idle_services g_idle_ops;

void process_idle_bind(const struct process_idle_services *ops)
{
	if (!ops)
	{
		g_idle_ops.poll = 0;
		g_idle_ops.on_quiescent = 0;
		return;
	}
	g_idle_ops = *ops;
}

const struct process_idle_services *process_idle_services(void)
{
	return &g_idle_ops;
}

void kernel_idle_loop(void)
{
	for (;;)
	{
		enable_interrupts();
		if (g_idle_ops.poll)
			g_idle_ops.poll();
		if (sched_count_runnable() > 1)
			sched_schedule_next();
		else if (g_idle_ops.on_quiescent)
			g_idle_ops.on_quiescent();
		else
			cpu_idle();
	}
}
