/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Round-Robin Scheduler
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Round-robin process scheduler with fair time-sharing
 *
 * This module implements a simple but effective round-robin scheduling
 * algorithm where processes are organized in a circular queue and each
 * process gets an equal time slice. This provides fairness and prevents
 * starvation, making it suitable for general-purpose workloads.
 *
 * Design decisions:
 * - Circular queue (FIFO) for simple O(1) insertion and selection
 * - State machine: READY -> RUNNING -> READY transitions
 * - Signal handling integrated before context switch
 * - Special handling for first context switch (kernel -> user transition)
 *
 * Thread safety:
 * - Called from interrupt context (timer IRQ)
 * - Must be reentrant but not necessarily SMP-safe
 * - Current implementation assumes single CPU
 */

#include "process.h"
#include "rr_sched.h"
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <arch/x86-64/sources/user_mode.h>
#include <ir0/signals.h>
#include <ir0/context.h>

/* Scheduler state - circular queue of runnable processes */
static rr_task_t *rr_head = NULL;   /* Head of circular queue (first to run) */
static rr_task_t *rr_tail = NULL;   /* Tail of circular queue (last to run) */
static rr_task_t *rr_current = NULL; /* Currently running process in queue */

/**
 * rr_add_process - Add a process to the round-robin scheduler
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
 * 4. Append to tail of circular queue (maintains order)
 * 5. Mark process as READY (eligible for scheduling)
 *
 * Complexity: O(1) - constant time insertion
 *
 * Thread safety: NOT thread-safe - caller must ensure mutual exclusion
 */
void rr_add_process(process_t *proc)
{
	rr_task_t *node;

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
	 * The node will be linked into the circular queue.
	 */
	node->process = proc;
	node->next = NULL;

	/* Insert into circular queue.
	 * If queue is empty, initialize head and tail to point to new node.
	 * Otherwise, append to tail and update tail pointer.
	 * This maintains FIFO order for fair scheduling.
	 */
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
	proc->state = PROCESS_READY;
}

/**
 * rr_remove_process - Remove a process from the round-robin scheduler
 * @proc: Process to remove (must be valid and in scheduler)
 *
 * Removes a process from the scheduler queue. This is used when a process
 * exits or becomes a zombie. The process structure remains in memory until
 * reaped by the parent, but it is no longer scheduled for execution.
 *
 * Algorithm:
 * 1. Find the scheduler node containing this process
 * 2. Remove node from circular queue (handle head, middle, tail cases)
 * 3. Free scheduler node
 * 4. Update rr_current if it pointed to removed node
 *
 * Complexity: O(n) worst case where n = number of processes
 * Could be optimized to O(1) with a doubly-linked list or hash table
 *
 * Thread safety: NOT thread-safe - caller must ensure mutual exclusion
 */
void rr_remove_process(process_t *proc)
{
	rr_task_t *current;
	rr_task_t *prev;
	
	if (!proc)
		return;
	
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
			return;
		}
		
		prev = current;
		current = current->next;
		
		/* Prevent infinite loop if queue is corrupted */
		if (current == rr_head)
			break;
	}
}

/**
 * rr_schedule_next - Select and switch to next process in round-robin order
 *
 * This is the core scheduling function called from the timer interrupt
 * handler. It implements round-robin scheduling by selecting the next
 * process in the circular queue and performing a context switch.
 *
 * Algorithm:
 * 1. Check if any processes are runnable (queue not empty)
 * 2. Select next process in round-robin fashion (circular traversal)
 * 3. Avoid unnecessary context switches (same process selected)
 * 4. Update process states (prev: RUNNING -> READY, next: READY -> RUNNING)
 * 5. Handle pending signals (before switching away from current process)
 * 6. Perform context switch:
 *    - First switch: Special kernel->user transition
 *    - Subsequent switches: Normal process-to-process switch
 *
 * Complexity: O(1) - constant time selection and switch
 *
 * Thread safety:
 * - Called from interrupt context (timer IRQ)
 * - Must not sleep or block
 * - Assumes interrupts are disabled during critical sections
 *
 * Side effects:
 * - Modifies current_process global
 * - Updates process states
 * - May trigger signal handlers
 * - Performs CPU context switch (does not return to caller)
 */
void rr_schedule_next(void)
{
	static int first = 1;  /* Track first context switch (kernel->user) */
	process_t *prev;        /* Process being switched away from */
	process_t *next;        /* Process being switched to */

	/* Early exit if no processes to schedule.
	 * This can happen during boot before any processes are created.
	 */
	if (!rr_head)
		return;

	/* Save reference to current process for state update.
	 * This is the process we're switching away from.
	 */
	prev = current_process;

	/* Round-robin selection: move to next process in circular queue.
	 * Skip processes that are zombies or not ready to run.
	 * If no current selection, start at head (first process).
	 * Otherwise, advance to next node, wrapping to head if at tail.
	 * This ensures fair time-sharing among all processes.
	 */
	if (!rr_current)
		rr_current = rr_head;  /* First selection - start at head */
	else
		rr_current = rr_current->next ? rr_current->next : rr_head;  /* Wrap around */

	/* Skip zombies and processes not ready to run */
	int attempts = 0;
	const int max_attempts = 100;  /* Prevent infinite loop */
	while (rr_current && attempts < max_attempts)
	{
		next = rr_current->process;
		
		/* Skip zombies - they should not be scheduled */
		if (next && next->state != PROCESS_ZOMBIE)
		{
			/* Found a runnable process */
			break;
		}
		
		/* Move to next process */
		rr_current = rr_current->next ? rr_current->next : rr_head;
		attempts++;
	}
	
	/* If no runnable process found, return */
	if (!rr_current || !next || next->state == PROCESS_ZOMBIE)
	{
		/* No runnable processes - halt CPU */
		current_process = NULL;
		__asm__ volatile("hlt");
		return;
	}

	BUG_ON(!next); /* Scheduler invariant: nodes must have valid process */

	/* Defensive check - should never happen due to BUG_ON above */
	if (!next)
		return;

	/* Optimization: avoid context switch if same process selected.
	 * This can happen if only one process is runnable or if the
	 * scheduler is called multiple times before a timer tick.
	 * Context switches are expensive, so we optimize this common case.
	 */
	if (!first && prev == next)
		return;

	/* Update process state machine.
	 * Previous process: RUNNING -> READY (now eligible for scheduling again)
	 * Next process: READY -> RUNNING (now executing on CPU)
	 */
	if (prev && prev->state == PROCESS_RUNNING)
		prev->state = PROCESS_READY;

	next->state = PROCESS_RUNNING;
	current_process = next;  /* Update global current process pointer */
	
	/* Handle pending signals before context switch.
	 * Signals must be delivered to the current process context before
	 * we switch away, otherwise signal handlers won't have access to
	 * the correct process state (registers, stack, etc).
	 */
	handle_signals();

	/* Special handling for first context switch.
	 * The first switch is unique because we're transitioning from
	 * kernel mode (where we're currently running) to user mode
	 * (where the first process will run). This requires special
	 * CPU state setup (segment selectors, privilege level, etc).
	 */
	if (first)
	{
		first = 0;  /* Mark that we've done the first switch */
		/* Jump to user mode - never returns */
		jmp_ring3((void *)next->task.rip);
		/* If we somehow return (should never happen), panic */
		panic("Returned from jmp_ring3");
	}

	/* Normal context switch between processes.
	 * Save current process context (registers, stack, etc) and
	 * restore next process context. This is architecture-specific
	 * and handled by switch_context_x64().
	 */
	if (prev && next) 
	{
		/* Architecture-specific context switch.
		 * Saves prev->task and restores next->task.
		 * Does not return to this function - returns to next process.
		 */
		switch_context_x64(&prev->task, &next->task);
	}
}
