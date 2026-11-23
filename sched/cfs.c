/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cfs.c
 * Description: UP CFS on one vruntime list; nice sets the weight, idle runs last.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "core.h"
#include <ir0/clock.h>
#include <ir0/kmem.h>
#include <ir0/problems.h>
#include <ir0/process.h>
#include <string.h>

/*
 * One singly-linked runqueue. Pick scans for the lowest vruntime; equal
 * vruntimes keep list order. There is no tree and no second policy.
 */
typedef struct cfs_task
{
	process_t *process;
	uint64_t vruntime;
	uint64_t last_charge;
	struct cfs_task *next;
} cfs_task_t;

/*
 * Everything a runqueue owns. There is one; SMP turns it into one per CPU
 * and the code below keeps working on the CPU's own queue.
 */
struct cfs_rq
{
	cfs_task_t *head;
	cfs_task_t *tail;
};

static struct cfs_rq cfs_rq;

/* Same predicate as clock_system.c clock_comm_is_idle. Kept local. */
static int cfs_comm_is_idle(const char *comm)
{
	if (!comm)
		return 0;
	return (comm[0] == 'i' && comm[1] == 'd' && comm[2] == 'l' &&
		comm[3] == 'e' && comm[4] == '\0');
}

/*
 * nice 0 weighs 1024. Each +1 nice multiplies by 4/5, each -1 by 5/4,
 * in integer arithmetic. The result is at least 1. Read on every charge.
 */
static uint64_t cfs_weight(int nice)
{
	uint64_t weight = 1024;
	int step;

	if (nice > 19)
		nice = 19;
	else if (nice < -20)
		nice = -20;

	if (nice > 0)
	{
		for (step = 0; step < nice; step++)
			weight = (weight * 4u) / 5u;
	}
	else if (nice < 0)
	{
		for (step = 0; step < -nice; step++)
			weight = (weight * 5u) / 4u;
	}

	if (weight < 1)
		weight = 1;
	return weight;
}

static cfs_task_t *cfs_find(process_t *proc)
{
	cfs_task_t *walk;

	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		if (walk->process == proc)
			return walk;
	}
	return NULL;
}

/*
 * Minimum vruntime of queued fair tasks. Idle is not a peer: its vruntime
 * must not pin this value. An empty fair queue yields 0.
 */
static uint64_t cfs_min_vruntime(void)
{
	cfs_task_t *walk;
	uint64_t min = 0;
	int seen = 0;

	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		if (!walk->process || cfs_comm_is_idle(walk->process->comm))
			continue;
		if (!seen || walk->vruntime < min)
		{
			min = walk->vruntime;
			seen = 1;
		}
	}
	return min;
}

static void cfs_unlink(cfs_task_t *node)
{
	cfs_task_t *prev = NULL;
	cfs_task_t *walk;

	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		if (walk != node)
		{
			prev = walk;
			continue;
		}

		if (prev)
			prev->next = node->next;
		else
			cfs_rq.head = node->next;
		if (cfs_rq.tail == node)
			cfs_rq.tail = prev;
		node->next = NULL;
		return;
	}
}

static void cfs_insert_before(cfs_task_t *node, cfs_task_t *before)
{
	cfs_task_t *prev;
	cfs_task_t *walk;

	if (!before)
	{
		node->next = NULL;
		if (!cfs_rq.head)
			cfs_rq.head = cfs_rq.tail = node;
		else
		{
			cfs_rq.tail->next = node;
			cfs_rq.tail = node;
		}
		return;
	}

	prev = NULL;
	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		if (walk != before)
		{
			prev = walk;
			continue;
		}

		node->next = before;
		if (prev)
			prev->next = node;
		else
			cfs_rq.head = node;
		return;
	}
}

/* Place @node first among tasks that share its vruntime. */
static void cfs_make_first_equal(cfs_task_t *node)
{
	cfs_task_t *walk;

	cfs_unlink(node);
	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		if (walk->vruntime == node->vruntime)
		{
			cfs_insert_before(node, walk);
			return;
		}
	}
	cfs_insert_before(node, NULL);
}

static void cfs_add_vruntime(cfs_task_t *node, uint64_t delta)
{
	process_t *proc;
	uint64_t weight;

	if (!node || delta == 0)
		return;

	proc = node->process;
	if (!proc)
		return;
	if (proc->state == PROCESS_BLOCKED || proc->state == PROCESS_ZOMBIE)
		return;

	weight = cfs_weight(proc->sched_nice);
	node->vruntime += (delta * 1024ull) / weight;
}

static void cfs_charge_running(void)
{
	cfs_task_t *walk;
	uint64_t now;

	now = clock_get_tick_count();
	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		uint64_t delta = 0;

		if (!walk->process || walk->process->state != PROCESS_RUNNING)
			continue;
		if (now >= walk->last_charge)
			delta = now - walk->last_charge;
		cfs_add_vruntime(walk, delta);
		walk->last_charge = now;
	}
}

/* Synthetic charge for the selftest. Does not read or store the tick. */
static void cfs_charge_delta(process_t *proc, uint64_t delta)
{
	cfs_task_t *node;
	unsigned long irq_flags;

	irq_flags = rq_lock();
	node = cfs_find(proc);
	if (node)
		cfs_add_vruntime(node, delta);
	rq_unlock(irq_flags);
}

static int cfs_is_runnable(process_t *proc)
{
	return proc && (proc->state == PROCESS_READY ||
			proc->state == PROCESS_RUNNING);
}

static process_t *cfs_choose(void)
{
	cfs_task_t *walk;
	cfs_task_t *best = NULL;
	cfs_task_t *idle = NULL;

	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		process_t *proc = walk->process;

		if (!cfs_is_runnable(proc))
			continue;
		if (cfs_comm_is_idle(proc->comm))
		{
			if (!idle)
				idle = walk;
			continue;
		}
		if (!best || walk->vruntime < best->vruntime)
			best = walk;
	}

	if (best)
		return best->process;
	if (idle)
		return idle->process;
	return NULL;
}

void sched_add(process_t *proc)
{
	cfs_task_t *node;
	unsigned long irq_flags;

	if (!proc)
		return;

	node = kmalloc(sizeof(cfs_task_t));
	if (!node)
	{
		BUG_ON(1);
		return;
	}

	node->process = proc;
	node->next = NULL;

	irq_flags = rq_lock();
	if (cfs_find(proc))
	{
		process_set_sched_state(proc, PROCESS_READY);
		rq_unlock(irq_flags);
		kfree(node);
		return;
	}

	node->vruntime = cfs_min_vruntime();
	node->last_charge = clock_get_tick_count();
	cfs_insert_before(node, NULL);
	process_set_sched_state(proc, PROCESS_READY);
	rq_unlock(irq_flags);
}

void sched_remove(process_t *proc)
{
	cfs_task_t *node;
	unsigned long irq_flags;

	if (!proc)
		return;

	irq_flags = rq_lock();
	node = cfs_find(proc);
	if (!node)
	{
		rq_unlock(irq_flags);
		return;
	}

	cfs_unlink(node);
	kfree(node);
	rq_unlock(irq_flags);
}

process_t *sched_pick_next(void)
{
	process_t *next;
	unsigned long irq_flags;

	irq_flags = rq_lock();
	cfs_charge_running();
	next = cfs_choose();
	if (next && next->state != PROCESS_RUNNING)
	{
		cfs_task_t *node = cfs_find(next);

		/*
		 * A task that waited READY must not be charged for the wait
		 * on the pick that follows this dispatch.
		 */
		if (node)
			node->last_charge = clock_get_tick_count();
	}
	rq_unlock(irq_flags);
	return next;
}

int sched_count_runnable(void)
{
	cfs_task_t *walk;
	int count = 0;
	unsigned long irq_flags;

	irq_flags = rq_lock();
	for (walk = cfs_rq.head; walk; walk = walk->next)
	{
		if (walk->process &&
		    (walk->process->state == PROCESS_READY ||
		     walk->process->state == PROCESS_RUNNING))
			count++;
	}
	rq_unlock(irq_flags);
	return count;
}

void sched_promote(process_t *proc)
{
	cfs_task_t *node;
	unsigned long irq_flags;

	if (!proc)
		return;

	irq_flags = rq_lock();
	node = cfs_find(proc);
	if (!node)
	{
		rq_unlock(irq_flags);
		return;
	}

	node->vruntime = cfs_min_vruntime();
	/*
	 * The sleep is not CPU time. The next charge starts when this task
	 * is dispatched, not at the tick where it last ran.
	 */
	node->last_charge = clock_get_tick_count();
	cfs_make_first_equal(node);
	process_set_sched_state(proc, PROCESS_READY);
	rq_unlock(irq_flags);
}

const char *sched_policy_name(void)
{
	return "cfs";
}

/*
 * Boot selftest: stack process_t, never switched. Charge one READY task by
 * an explicit delta; the next pick must be the other. Promote the heavy
 * task and the following pick must return it. Then the queue is empty.
 */
int sched_selftest(void)
{
	process_t light;
	process_t heavy;
	process_t *pick;
	int rc = 0;

	memset(&light, 0, sizeof(light));
	memset(&heavy, 0, sizeof(heavy));
	light.state = PROCESS_READY;
	heavy.state = PROCESS_READY;
	light.task.pid = 9001;
	heavy.task.pid = 9002;

	sched_add(&light);
	sched_add(&heavy);
	cfs_charge_delta(&heavy, 100000ull);

	pick = sched_pick_next();
	if (pick != &light)
	{
		rc = -1;
		goto out;
	}

	sched_promote(&heavy);
	pick = sched_pick_next();
	if (pick != &heavy)
		rc = -1;

out:
	sched_remove(&light);
	sched_remove(&heavy);
	if (rc == 0 && sched_count_runnable() != 0)
		rc = -1;
	return rc;
}
