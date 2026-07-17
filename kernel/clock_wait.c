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
#include <ir0/errno.h>
#include <ir0/sched.h>
#include <ir0/arch_port.h>

extern void kernel_idle_poll(void);
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

	proc->state = PROCESS_READY;
	sched_add_process(proc);
	sched_promote_process(proc);
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
		/*
		 * Poll/pipe/wait4 use irq_frame_saved or poll_waiter wake paths;
		 * only nanosleep/pause-style kernel sleeps use the timer queue.
		 */
		if (p->irq_frame_saved || p->poll_waiter)
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
	arch_enable_interrupts();

	for (i = 0; i < 32; i++)
	{
		arch_cpu_idle();
		kernel_idle_poll();
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
		sched_schedule_next();
}

int ir0_clock_wait_block_until(uint64_t deadline_ms)
{
	process_t *proc;

	proc = current_process;
	if (!proc)
		return -ESRCH;

	if (clock_get_uptime_milliseconds() >= deadline_ms)
		return 0;

	ir0_clock_wait_arm(proc, deadline_ms);
	proc->state = PROCESS_BLOCKED;
	process_arm_kernel_syscall_sleep(proc);

	while (proc->state == PROCESS_BLOCKED)
	{
		ir0_clock_wait_idle_step();
		if (ir0_clock_should_yield_runqueue(proc))
			sched_schedule_next();
		kernel_idle_poll();

		if (proc->state != PROCESS_BLOCKED)
			break;
		if (sched_count_runnable() == 0)
			arch_cpu_idle();
	}

	ir0_clock_wait_disarm(proc);
	process_restore_user_task_segments(proc);

	if (proc->syscall_interrupted)
	{
		proc->syscall_interrupted = 0;
		return -EINTR;
	}

	return 0;
}

void sleep_wake_check(void)
{
	ir0_clock_wait_fire_due(clock_get_uptime_milliseconds());
}
