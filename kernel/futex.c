/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: futex.c
 * Description: futex wait queues (musl pthread subset)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/futex.h>
#include <ir0/process.h>
#include <ir0/sched.h>
#include <ir0/signals.h>

#define IR0_FUTEX_MAX_WAITERS 64

/*
 * A waiter lives on the sleeping task's kernel stack (Linux futex_q). The
 * table only points at it; wake and exit unhook the pointer.
 */
struct ir0_futex_waiter
{
	process_t *proc;
	int *uaddr;
	int woken;
};

static struct ir0_futex_waiter *futex_waiters[IR0_FUTEX_MAX_WAITERS];

void (*ir0_futex_before_sleep)(process_t *proc);

/*
 * ir0_futex_wait - Sleep until ir0_futex_wake() on @uaddr or a signal.
 *
 * The caller has checked *uaddr == @val. Returns 0 when woken, -EAGAIN when
 * every waiter slot is taken, -ERESTARTSYS when a signal ends the wait.
 */
int ir0_futex_wait(process_t *proc, int *uaddr, int val)
{
	struct ir0_futex_waiter w;
	int slot;
	int ret;

	(void)val;
	if (!proc || !uaddr)
		return -EINVAL;

	for (slot = 0; slot < IR0_FUTEX_MAX_WAITERS; slot++)
	{
		if (!futex_waiters[slot])
			break;
	}
	if (slot == IR0_FUTEX_MAX_WAITERS)
		return -EAGAIN;

	w.proc = proc;
	w.uaddr = uaddr;
	w.woken = 0;
	futex_waiters[slot] = &w;

	for (;;)
	{
		/*
		 * prepare_to_wait: BLOCKED before the tests, so a wake or a
		 * signal that lands in between sets READY instead of being lost.
		 */
		process_set_sched_state(proc, PROCESS_BLOCKED);
		if (w.woken)
		{
			ret = 0;
			break;
		}
		if (signal_pending(proc))
		{
			ret = -ERESTARTSYS;
			break;
		}
		if (ir0_futex_before_sleep)
			ir0_futex_before_sleep(proc);
		/* A wake since the tests already made us READY: test again. */
		if (proc->state == PROCESS_BLOCKED)
			schedule();
	}

	process_set_sched_state(proc, PROCESS_READY);
	if (futex_waiters[slot] == &w)
		futex_waiters[slot] = NULL;
	return ret;
}

int ir0_futex_wake(int *uaddr, int count)
{
	int i;
	int woke = 0;

	if (!uaddr || count <= 0)
		return 0;

	for (i = 0; i < IR0_FUTEX_MAX_WAITERS && woke < count; i++)
	{
		struct ir0_futex_waiter *w = futex_waiters[i];

		if (!w || w->uaddr != uaddr)
			continue;
		futex_waiters[i] = NULL;
		w->woken = 1;
		process_set_sched_state(w->proc, PROCESS_READY);
		woke++;
	}
	return woke;
}

void ir0_futex_drop_process(process_t *proc)
{
	int i;

	if (!proc)
		return;
	for (i = 0; i < IR0_FUTEX_MAX_WAITERS; i++)
	{
		if (futex_waiters[i] && futex_waiters[i]->proc == proc)
			futex_waiters[i] = NULL;
	}
}
