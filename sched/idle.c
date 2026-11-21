/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: idle.c
 * Description: The idle task: poll interrupt-less devices, run woken tasks,
 *              halt until the next interrupt.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cpu.h>
#include <ir0/idle.h>
#include <ir0/sched.h>

#define KERNEL_IDLE_POLLS 8

/*
 * Polls for devices that may not raise an interrupt. Each owner registers
 * its own function from its init. A missing owner never registers, so the
 * idle task does not call a no-op. Sleeping tasks never run this list:
 * they wait in schedule() for a wake.
 */

static void (*idle_polls[KERNEL_IDLE_POLLS])(void);
static int idle_poll_count;

void kernel_idle_poll_register(void (*poll)(void))
{
	int i;

	if (!poll)
		return;

	for (i = 0; i < idle_poll_count; i++)
	{
		if (idle_polls[i] == poll)
			return;
	}

	if (idle_poll_count >= KERNEL_IDLE_POLLS)
		return;

	idle_polls[idle_poll_count] = poll;
	idle_poll_count++;
}

void kernel_idle_loop(void)
{
	int i;

	for (;;)
	{
		enable_interrupts();
		for (i = 0; i < idle_poll_count; i++)
			idle_polls[i]();
		if (sched_count_runnable() > 1)
			schedule();
		else
			cpu_idle();
	}
}
