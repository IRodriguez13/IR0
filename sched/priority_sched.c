/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: priority_sched.c
 * Description: Fixed priority-band runqueues (RR within band); not CFS.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "priority_sched.h"
#include "rr_sched.h"
#include <ir0/arch_port.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <ir0/serial_io.h>
#include <string.h>

typedef struct prio_task
{
	process_t *process;
	struct prio_task *next;
} prio_task_t;

typedef struct prio_band
{
	prio_task_t *head;
	prio_task_t *tail;
	prio_task_t *curr; /* last selected in this band (RR cursor) */
} prio_band_t;

static prio_band_t prio_bands[IR0_SCHED_PRIO_BANDS];

static inline uint64_t prio_irq_save(void)
{
	return (uint64_t)arch_irq_save();
}

static inline void prio_irq_restore(uint64_t flags)
{
	arch_irq_restore((unsigned long)flags);
}

static int prio_clamp(int prio)
{
	if (prio < 0)
		return 0;
	if (prio > IR0_SCHED_PRIO_MAX)
		return IR0_SCHED_PRIO_MAX;
	return prio;
}

static int prio_band_of(process_t *proc)
{
	if (!proc)
		return IR0_SCHED_PRIO_DEFAULT;
	return prio_clamp(proc->sched_prio);
}

static int prio_already_queued(process_t *proc)
{
	int b;
	prio_task_t *scan;

	for (b = 0; b < IR0_SCHED_PRIO_BANDS; b++)
	{
		for (scan = prio_bands[b].head; scan; scan = scan->next)
		{
			if (scan->process == proc)
				return 1;
		}
	}
	return 0;
}

void priority_add_process(process_t *proc)
{
	prio_task_t *node;
	prio_band_t *band;
	uint64_t irq_flags;
	int b;

	if (!proc)
		return;

	node = kmalloc(sizeof(prio_task_t));
	if (!node)
	{
		BUG_ON(1);
		return;
	}

	node->process = proc;
	node->next = NULL;
	b = prio_band_of(proc);

	irq_flags = prio_irq_save();

	if (prio_already_queued(proc))
	{
		proc->state = PROCESS_READY;
		prio_irq_restore(irq_flags);
		kfree(node);
		return;
	}

	band = &prio_bands[b];
	if (!band->head)
		band->head = band->tail = node;
	else
	{
		band->tail->next = node;
		band->tail = node;
	}

	proc->state = PROCESS_READY;
	prio_irq_restore(irq_flags);
}

void priority_remove_process(process_t *proc)
{
	prio_task_t *cur;
	prio_task_t *prev;
	uint64_t irq_flags;
	int b;

	if (!proc)
		return;

	irq_flags = prio_irq_save();

	for (b = 0; b < IR0_SCHED_PRIO_BANDS; b++)
	{
		prev = NULL;
		cur = prio_bands[b].head;
		while (cur)
		{
			if (cur->process == proc)
			{
				if (prev)
					prev->next = cur->next;
				else
					prio_bands[b].head = cur->next;

				if (cur == prio_bands[b].tail)
					prio_bands[b].tail = prev;

				if (prio_bands[b].curr == cur)
					prio_bands[b].curr = cur->next;

				kfree(cur);
				prio_irq_restore(irq_flags);
				return;
			}
			prev = cur;
			cur = cur->next;
		}
	}

	prio_irq_restore(irq_flags);
}

static process_t *priority_pick_next(void)
{
	int b;
	int attempts;
	prio_task_t *start;
	prio_task_t *walk;
	process_t *cand;

	for (b = IR0_SCHED_PRIO_MAX; b >= 0; b--)
	{
		if (!prio_bands[b].head)
			continue;

		if (!prio_bands[b].curr)
			prio_bands[b].curr = prio_bands[b].head;
		else if (prio_bands[b].curr->next)
			prio_bands[b].curr = prio_bands[b].curr->next;
		else
			prio_bands[b].curr = prio_bands[b].head;

		start = prio_bands[b].curr;
		walk = start;
		attempts = 0;
		do
		{
			cand = walk ? walk->process : NULL;
			if (cand && cand->state != PROCESS_ZOMBIE &&
			    cand->state != PROCESS_BLOCKED)
			{
				prio_bands[b].curr = walk;
				return cand;
			}
			walk = walk->next ? walk->next : prio_bands[b].head;
			attempts++;
		} while (walk && walk != start && attempts < 64);
	}

	return NULL;
}

void priority_schedule_next(void)
{
	process_t *next;
	uint64_t irq_flags = prio_irq_save();

	next = priority_pick_next();
	prio_irq_restore(irq_flags);

	if (!next)
		return;

	sched_context_switch_to(next);
}

int priority_count_runnable(void)
{
	int b;
	int count = 0;
	prio_task_t *walk;
	uint64_t irq_flags = prio_irq_save();

	for (b = 0; b < IR0_SCHED_PRIO_BANDS; b++)
	{
		for (walk = prio_bands[b].head; walk; walk = walk->next)
		{
			if (walk->process &&
			    (walk->process->state == PROCESS_READY ||
			     walk->process->state == PROCESS_RUNNING))
				count++;
		}
	}

	prio_irq_restore(irq_flags);
	return count;
}

void priority_promote_process(process_t *proc)
{
	int b;
	prio_task_t *walk;
	uint64_t irq_flags;

	if (!proc)
		return;

	irq_flags = prio_irq_save();
	b = prio_band_of(proc);
	for (walk = prio_bands[b].head; walk; walk = walk->next)
	{
		if (walk->process == proc)
		{
			prio_bands[b].curr = walk;
			proc->state = PROCESS_READY;
			break;
		}
	}
	prio_irq_restore(irq_flags);
}

/*
 * Boot selftest: higher band must be picked before a lower band when both READY.
 * Uses stack-backed process_t stubs (never scheduled for real).
 */
int priority_sched_selftest(void)
{
	process_t lo;
	process_t hi;
	process_t *pick;
	uint64_t irq_flags;

	memset(&lo, 0, sizeof(lo));
	memset(&hi, 0, sizeof(hi));
	lo.state = PROCESS_READY;
	hi.state = PROCESS_READY;
	lo.sched_prio = 1;
	hi.sched_prio = 6;
	lo.task.pid = 9001;
	hi.task.pid = 9002;

	priority_add_process(&lo);
	priority_add_process(&hi);

	irq_flags = prio_irq_save();
	pick = priority_pick_next();
	prio_irq_restore(irq_flags);

	priority_remove_process(&lo);
	priority_remove_process(&hi);

	if (pick != &hi)
		return -1;
	if (priority_count_runnable() != 0)
		return -1;
	return 0;
}
