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
#include <ir0/errno.h>
#include <ir0/copy_user.h>
#include <string.h>
#include <ir0/kernel.h>

#define IR0_FUTEX_MAX_WAITERS 64

struct ir0_futex_slot
{
	process_t *proc;
	int *uaddr;
	int val;
	uint8_t in_use;
};

static struct ir0_futex_slot futex_waiters[IR0_FUTEX_MAX_WAITERS];

void (*ir0_futex_before_sleep)(process_t *proc);

static void futex_slot_release_if_mine(int slot, process_t *proc)
{
	if (slot < 0 || slot >= IR0_FUTEX_MAX_WAITERS || !proc)
		return;
	if (!futex_waiters[slot].in_use || futex_waiters[slot].proc != proc)
		return;
	futex_waiters[slot].in_use = 0;
	futex_waiters[slot].proc = NULL;
	futex_waiters[slot].uaddr = NULL;
}

int ir0_futex_wait(process_t *proc, int *uaddr, int val)
{
	int i;

	if (!proc || !uaddr)
		return -EINVAL;

	for (i = 0; i < IR0_FUTEX_MAX_WAITERS; i++)
	{
		if (!futex_waiters[i].in_use)
		{
			futex_waiters[i].proc = proc;
			futex_waiters[i].uaddr = uaddr;
			futex_waiters[i].val = val;
			futex_waiters[i].in_use = 1;
			process_set_sched_state(proc, PROCESS_BLOCKED);
			while (proc->state == PROCESS_BLOCKED)
			{
				if (ir0_futex_before_sleep)
					ir0_futex_before_sleep(proc);
				if (proc->state != PROCESS_BLOCKED)
					break;
				schedule();
				if (proc->state != PROCESS_BLOCKED)
					break;
				kernel_idle_poll();
			}
			/*
			 * futex_wake already clears the slot. Any other exit
			 * (signal, console) leaves it occupied, and the next
			 * waiter eventually gets -EAGAIN. Release only if this
			 * task still owns the slot: wake may have reused it.
			 */
			futex_slot_release_if_mine(i, proc);
			if (proc->syscall_interrupted)
			{
				proc->syscall_interrupted = 0;
				return -EINTR;
			}
			return 0;
		}
	}
	return -EAGAIN;
}

int ir0_futex_wake(int *uaddr, int count)
{
	int i;
	int woke = 0;

	if (!uaddr || count <= 0)
		return 0;

	for (i = 0; i < IR0_FUTEX_MAX_WAITERS && woke < count; i++)
	{
		if (!futex_waiters[i].in_use)
			continue;
		if (futex_waiters[i].uaddr != uaddr)
			continue;
		if (futex_waiters[i].proc)
			process_set_sched_state(futex_waiters[i].proc, PROCESS_READY);
		futex_waiters[i].in_use = 0;
		futex_waiters[i].proc = NULL;
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
		if (futex_waiters[i].in_use && futex_waiters[i].proc == proc)
		{
			futex_waiters[i].in_use = 0;
			futex_waiters[i].proc = NULL;
		}
	}
}
