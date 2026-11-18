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
#include <ir0/time.h>

/* Seconds beyond this saturate: the deadline is then never reached. */
#define CLOCK_DEADLINE_MAX_SEC (1ULL << 40)
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_SEC 1000000000L

extern process_t *process_list;
extern process_t *current_process;

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

/* CLOCK_REALTIME in ms, as clock_gettime(2) reports it, at uptime @now. */
static uint64_t clock_realtime_ms(uint64_t now)
{
	if (!clock_realtime_available())
		return now;
	return (uint64_t)clock_get_current_time() * 1000ULL + now % 1000ULL;
}

int ir0_clock_deadline_ms(const struct timespec *ts, int absolute,
			  int realtime, uint64_t *deadline_ms)
{
	uint64_t now = clock_get_uptime_milliseconds();
	uint64_t base;
	uint64_t ms;

	if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;
	if ((uint64_t)ts->tv_sec >= CLOCK_DEADLINE_MAX_SEC)
	{
		*deadline_ms = IR0_CLOCK_WAIT_DISARMED - 1;
		return 0;
	}
	ms = (uint64_t)ts->tv_sec * 1000ULL +
	     (uint64_t)(ts->tv_nsec + NSEC_PER_MSEC - 1) / NSEC_PER_MSEC;
	if (!absolute)
	{
		*deadline_ms = now + ms;
		return 0;
	}
	base = realtime ? clock_realtime_ms(now) : now;
	*deadline_ms = ms > base ? now + (ms - base) : now;
	return 0;
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

		/* The timer IRQ (ir0_clock_wait_fire_due) wakes us. */
		schedule();
	}

	process_set_sched_state(proc, PROCESS_READY);
	ir0_clock_wait_disarm(proc);
	return ret;
}
