/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: clock_wait.c
 * Description: Per-process monotonic timer waits — clockevent wake path for blocking syscalls
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/clock_wait.h>
#include <ir0/clock.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/signals.h>
#include <ir0/arch_port.h>
#include <ir0/kernel.h>

extern process_t *process_list;
extern process_t *current_process;

/*
 * Yield the CPU from a syscall sleep loop when another task can run.
 * A BLOCKED waiter is not counted in sched_count_runnable(), so "> 1"
 * never triggers for pause(2) with only the parent READY — fork/pause/kill
 * would deadlock with the child spinning as current_process.
 */
static int ir0_clock_should_yield_runqueue(process_t *self)
{
	if (self && self->state == PROCESS_BLOCKED)
		return sched_count_runnable() >= 1;

	return sched_count_runnable() > 1;
}

static void ir0_clock_wait_wake_process(process_t *proc)
{
	if (!proc || proc->state != PROCESS_BLOCKED)
		return;

	process_set_sched_state(proc, PROCESS_READY);
	sched_add(proc);
	sched_promote(proc);
}

void ir0_clock_wait_arm(process_t *proc, uint64_t deadline_ms)
{
	if (!proc)
		return;

	proc->clock_wait_deadline_ms = deadline_ms;
	proc->clock_wait_armed = 1;
}

void ir0_clock_wait_disarm(process_t *proc)
{
	if (!proc)
		return;

	proc->clock_wait_armed = 0;
	proc->clock_wait_deadline_ms = IR0_CLOCK_WAIT_DISARMED;
}

void ir0_clock_wait_fire_due(uint64_t now_ms)
{
	process_t *p;

	for (p = process_list; p; p = p->next)
	{
		/* poll_waiter owns its own timeout (poll_wake_do). */
		if (p->poll_waiter)
			continue;
		if (!p->clock_wait_armed)
			continue;
		if (now_ms < p->clock_wait_deadline_ms)
			continue;

		ir0_clock_wait_disarm(p);
		ir0_clock_wait_wake_process(p);
	}
}

/*
 * ir0_clock_wait_idle_step - Wait for PIT/clockevent; emulate one tick if starved.
 *
 * QEMU + cooperative peers may run for stretches without a hardware tick while
 * IF=1; advance clock_tick() once so ir0_clock_wait_fire_due() can unblock waiters.
 */
static void ir0_clock_wait_idle_step(void)
{
	uint64_t t0;
	unsigned int i;

	t0 = clock_get_uptime_milliseconds();
	enable_interrupts();

	for (i = 0; i < 32; i++)
	{
		cpu_idle();
		/*
		 * Nosched wakes only — block_until / service_runqueue issue one
		 * schedule per loop. Nested schedule from
		 * poll_wake_check/stdin_wake_check caused Class B on desk.
		 */
		kernel_idle_poll_nosched();
		if (clock_get_uptime_milliseconds() != t0)
			break;
	}

	if (clock_get_uptime_milliseconds() == t0)
		clock_tick();
}

void ir0_clock_wait_service_clockevents(void)
{
	ir0_clock_wait_idle_step();
}

void ir0_clock_wait_service_runqueue(void)
{
	ir0_clock_wait_idle_step();
	if (ir0_clock_should_yield_runqueue(current_process))
		schedule();
}

int ir0_clock_wait_block_until(uint64_t deadline_ms)
{
	process_t *proc;
	int ret = 0;

	proc = current_process;
	if (!proc)
		return -ESRCH;

	ir0_clock_wait_arm(proc, deadline_ms);
	for (;;)
	{
		/*
		 * prepare_to_wait: BLOCKED before the tests, so a signal or the
		 * deadline firing in between sets READY instead of being lost.
		 * Only the deadline or a signal ends the sleep; any other wake
		 * goes back to sleep.
		 */
		process_set_sched_state(proc, PROCESS_BLOCKED);
		if (clock_get_uptime_milliseconds() >= deadline_ms)
			break;
		if (signal_pending(proc))
		{
			ret = -EINTR;
			break;
		}
		if (!proc->clock_wait_armed)
			ir0_clock_wait_arm(proc, deadline_ms);

		ir0_clock_wait_idle_step();
		if (proc->state == PROCESS_BLOCKED &&
		    ir0_clock_should_yield_runqueue(proc))
			schedule();
	}

	process_set_sched_state(proc, PROCESS_READY);
	ir0_clock_wait_disarm(proc);
	return ret;
}

void sleep_wake_check(void)
{
	ir0_clock_wait_fire_due(clock_get_uptime_milliseconds());
}
