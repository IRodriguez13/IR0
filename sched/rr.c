/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: backend.c
 * Description: IR0 Kernel - Round-Robin Scheduler
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "core.h"
#include <ir0/process.h>
#include <ir0/kmem.h>
#include <ir0/problems.h>

/*
 * Ready list is singly linked (head -> ... -> tail, next=NULL). Advance wraps
 * with `next ? next : rr_head`; it is not a circular queue.
 */
typedef struct rr_task
{
	process_t *process;
	struct rr_task *next;
} rr_task_t;

static rr_task_t *rr_head = NULL;
static rr_task_t *rr_tail = NULL;
static rr_task_t *rr_current = NULL;

/*
 * Bound a pick walk if the list is dirty with zombies/blocked tasks.
 * Prevents livelock; not a statement about maximum process count.
 */
#define RR_MAX_PICK_ATTEMPTS 100

/**
 * sched_add - Add a process to the round-robin scheduler
 * @proc: Process to add (must be valid and not already in scheduler)
 *
 * Inserts a new process into the round-robin queue at the tail,
 * maintaining FIFO order. The process is marked as READY and will
 * be scheduled on the next scheduling pass.
 *
 * Algorithm:
 * 1. Validate process pointer (prevent NULL pointer dereference)
 * 2. Allocate scheduler node (panic on failure - critical path)
 * 3. Link process to scheduler node
 * 4. Append to tail of the ready list (maintains FIFO order)
 * 5. Mark process as READY (eligible for scheduling)
 *
 * Complexity: O(1) - constant time insertion
 *
 * Thread safety: NOT thread-safe - caller must ensure mutual exclusion
 */
void sched_add(process_t *proc)
{
	rr_task_t *scan;
	rr_task_t *node;
	unsigned long irq_flags;

	/* Sanity check - prevent adding NULL processes.
	 * This is a defensive check for API misuse.
	 */
	if (!proc)
		return;

	/* Allocate scheduler node - must succeed.
	 * Out of memory in the scheduler is a critical failure since
	 * we can't schedule new processes. Panic if allocation fails.
	 */
	node = kmalloc(sizeof(rr_task_t));
	if (!node)
	{
		BUG_ON(1); /* Out of memory in scheduler - unrecoverable */
		return;
	}

	BUG_ON(!proc); /* Invalid process parameter - should never happen */
	
	/* Initialize scheduler node with process pointer.
	 * The node will be linked into the ready list.
	 */
	node->process = proc;
	node->next = NULL;

	irq_flags = rq_lock();

	/* Insert at tail of the singly-linked ready list.
	 * If queue is empty, initialize head and tail to point to new node.
	 * Otherwise, append to tail and update tail pointer.
	 * This maintains FIFO order for fair scheduling.
	 */
	for (scan = rr_head; scan; scan = scan->next)
	{
		if (scan->process == proc)
		{
			/* Process already queued: only ensure READY state. */
			process_set_sched_state(proc, PROCESS_READY);
			rq_unlock(irq_flags);
			kfree(node);
			return;
		}
	}

	if (!rr_head)
	{
		/* First process - initialize queue */
		rr_head = rr_tail = node;
	}
	else
	{
		/* Append to tail - maintains order */
		rr_tail->next = node;
		rr_tail = node;
	}

	/* Mark process as ready for scheduling.
	 * State machine: NEW -> READY -> RUNNING -> READY -> ...
	 */
	process_set_sched_state(proc, PROCESS_READY);
	rq_unlock(irq_flags);
}

/**
 * sched_remove - Remove a process from the round-robin scheduler
 * @proc: Process to remove (must be valid and in scheduler)
 *
 * Removes a process from the scheduler queue. This is used when a process
 * exits or becomes a zombie. The process structure remains in memory until
 * reaped by the parent, but it is no longer scheduled for execution.
 *
 * Algorithm:
 * 1. Find the scheduler node containing this process
 * 2. Remove node from the ready list (handle head, middle, tail cases)
 * 3. Free scheduler node
 * 4. Update rr_current if it pointed to removed node
 *
 * Complexity: O(n) worst case where n = number of processes
 * Could be optimized to O(1) with a doubly-linked list or hash table
 *
 * Thread safety: NOT thread-safe - caller must ensure mutual exclusion
 */
void sched_remove(process_t *proc)
{
	rr_task_t *current;
	rr_task_t *prev;
	unsigned long irq_flags;
	
	if (!proc)
		return;

	irq_flags = rq_lock();
	
	/* Find the scheduler node containing this process */
	current = rr_head;
	prev = NULL;
	
	while (current)
	{
		if (current->process == proc)
		{
			/* Found it - remove from queue */
			
			/* Update queue pointers */
			if (prev)
			{
				/* Node is in middle or at tail */
				prev->next = current->next;
				if (current == rr_tail)
					rr_tail = prev;
			}
			else
			{
				/* Node is at head */
				rr_head = current->next;
				if (!rr_head)
					rr_tail = NULL;  /* Queue is now empty */
			}
			
			/* If rr_current points to this node, advance it */
			if (rr_current == current)
			{
				rr_current = current->next;
				if (!rr_current && rr_head)
					rr_current = rr_head;  /* Wrap to head if needed */
			}
			
			/* Free scheduler node */
			kfree(current);
			rq_unlock(irq_flags);
			return;
		}
		
		prev = current;
		current = current->next;
		
		/* Prevent infinite loop if queue is corrupted */
		if (current == rr_head)
			break;
	}

	rq_unlock(irq_flags);
}

/**
 * sched_pick_next - Select the next process in round-robin order
 */
process_t *sched_pick_next(void)
{
	process_t *next = NULL;
	unsigned long irq_flags = rq_lock();
	int attempts = 0;

	if (!rr_head)
	{
		rq_unlock(irq_flags);
		return NULL;
	}

	if (!rr_current)
		rr_current = rr_head;
	else
		rr_current = rr_current->next ? rr_current->next : rr_head;

	while (rr_current && attempts < RR_MAX_PICK_ATTEMPTS)
	{
		next = rr_current->process;

		if (next && next->state != PROCESS_ZOMBIE && next->state != PROCESS_BLOCKED)
			break;

		rr_current = rr_current->next ? rr_current->next : rr_head;
		attempts++;
	}

	if (!rr_current || !next || next->state == PROCESS_ZOMBIE ||
	    next->state == PROCESS_BLOCKED)
	{
		rq_unlock(irq_flags);
		return NULL;
	}

	BUG_ON(!next);
	rq_unlock(irq_flags);
	return next;
}

int sched_count_runnable(void)
{
	rr_task_t *walk;
	int count = 0;
	unsigned long irq_flags;

	if (!rr_head)
		return 0;

	irq_flags = rq_lock();
	walk = rr_head;
	do
	{
		if (walk->process &&
		    (walk->process->state == PROCESS_READY ||
		     walk->process->state == PROCESS_RUNNING))
			count++;
		walk = walk->next;
	} while (walk && walk != rr_head);

	rq_unlock(irq_flags);
	return count;
}

void sched_promote(process_t *proc)
{
	rr_task_t *walk;
	rr_task_t *prev = NULL;
	unsigned long irq_flags;

	if (!proc || !rr_head)
		return;

	irq_flags = rq_lock();
	for (walk = rr_head; walk; walk = walk->next)
	{
		if (walk->process == proc)
		{
			/*
			 * sched_pick_next() advances the cursor before selecting.
			 * Point at the predecessor so the promoted task is the next
			 * candidate.  Pointing at @walk skipped the very task that the
			 * wake path intended to run.
			 */
			rr_current = prev ? prev : rr_tail;
			process_set_sched_state(proc, PROCESS_READY);
			break;
		}
		prev = walk;
		if (walk->next == rr_head)
			break;
	}
	rq_unlock(irq_flags);
}

const char *sched_policy_name(void)
{
	/* Policy 1 is an honest CFS name alias over the round-robin queue. */
	return CONFIG_SCHEDULER_POLICY == 1 ? "cfs" : "round_robin";
}

/* FIFO rotation has no ordering invariant to check at boot. */
int sched_selftest(void)
{
	return 0;
}
