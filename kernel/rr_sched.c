
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
 * Thread safety:
 * - Called from interrupt context (timer IRQ)
 * - Must be reentrant but not necessarily SMP-safe
 * - Current implementation assumes single CPU
 */

#include "process.h"
#include "rr_sched.h"
#include <config.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <arch/common/arch_portable.h>
#include <ir0/signals.h>
#include <ir0/context.h>
#include <stdint.h>

/* Scheduler state - circular queue of runnable processes */
static rr_task_t *rr_head = NULL;   /* Head of circular queue (first to run) */
static rr_task_t *rr_tail = NULL;   /* Tail of circular queue (last to run) */
static rr_task_t *rr_current = NULL; /* Currently running process in queue */

/*
 * UP scheduler critical sections:
 * serialize scheduler queue mutations against timer IRQ scheduling.
 */
static inline uint64_t rr_irq_save(void)
{
#if defined(__x86_64__) || defined(__i386__)
	uint64_t flags;
	__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
	return flags;
#else
	arch_disable_interrupts();
	return 0;
#endif
}

static inline void rr_irq_restore(uint64_t flags)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
	(void)flags;
	arch_enable_interrupts();
#endif
}

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
	rr_task_t *scan;
	rr_task_t *node;
	uint64_t irq_flags;

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

	irq_flags = rr_irq_save();

	/* Insert into circular queue.
	 * If queue is empty, initialize head and tail to point to new node.
	 * Otherwise, append to tail and update tail pointer.
	 * This maintains FIFO order for fair scheduling.
	 */
	for (scan = rr_head; scan; scan = scan->next)
	{
		if (scan->process == proc)
		{
			/* Process already queued: only ensure READY state. */
			proc->state = PROCESS_READY;
			rr_irq_restore(irq_flags);
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
	proc->state = PROCESS_READY;
	rr_irq_restore(irq_flags);
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
	uint64_t irq_flags;
	
	if (!proc)
		return;

	irq_flags = rr_irq_save();
	
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
			rr_irq_restore(irq_flags);
			return;
		}
		
		prev = current;
		current = current->next;
		
		/* Prevent infinite loop if queue is corrupted */
		if (current == rr_head)
			break;
	}

	rr_irq_restore(irq_flags);
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
	process_t *next = NULL; /* Process being switched to */
	uint64_t irq_flags = rr_irq_save();

	/* Early exit if no processes to schedule.
	 * This can happen during boot before any processes are created.
	 */
	if (!rr_head)
	{
		rr_irq_restore(irq_flags);
		return;
	}

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
		
		/* Skip zombies and blocked (e.g. waiting in poll) */
		if (next && next->state != PROCESS_ZOMBIE && next->state != PROCESS_BLOCKED)
		{
			/* Found a runnable process */
			break;
		}
		
		/* Move to next process */
		rr_current = rr_current->next ? rr_current->next : rr_head;
		attempts++;
	}
	
	/* If no runnable process found (e.g. all blocked on read/poll) */
	if (!rr_current || !next || next->state == PROCESS_ZOMBIE || next->state == PROCESS_BLOCKED)
	{
		/*
		 * Never execute HLT here because this function can run from IRQ context
		 * with IF=0. Halting with interrupts disabled deadlocks the CPU.
		 *
		 * Keep current_process unchanged and return to the interrupted context;
		 * the main idle loop is responsible for HLT in a safe context.
		 */
		rr_irq_restore(irq_flags);
		return;
	}

	BUG_ON(!next); /* Scheduler invariant: nodes must have valid process */

	/* Defensive check - should never happen due to BUG_ON above */
	if (!next)
	{
		rr_irq_restore(irq_flags);
		return;
	}

	/* Optimization: avoid context switch if same process selected.
	 * This can happen if only one process is runnable or if the
	 * scheduler is called multiple times before a timer tick.
	 * Context switches are expensive, so we optimize this common case.
	 */
	if (!first && prev == next)
	{
		rr_irq_restore(irq_flags);
		return;
	}

	/* Update process state machine.
	 * Previous process: RUNNING -> READY (now eligible for scheduling again)
	 * Next process: READY -> RUNNING (now executing on CPU)
	 */
	int should_handle_signals = 0;
	if (prev && prev->state == PROCESS_RUNNING)
	{
		should_handle_signals = 1;
		prev->state = PROCESS_READY;
	}

	/* Handle pending signals on the outgoing context before switching.
	 * This keeps signal processing aligned with the process state we are
	 * about to leave, avoiding delivery on the wrong task.
	 */
	if (should_handle_signals)
		handle_signals();

	next->state = PROCESS_RUNNING;
	current_process = next;  /* Update global current process pointer */

	/*
	 * First context switch: no previous task to save.
	 * For kernel-mode processes (debug shell), stay in ring 0.
	 * For user-mode processes, transition to ring 3.
	 */
	if (first)
	{
		first = 0;
#if defined(ARCH_X86_64) || defined(ARCH_X86)
		if (next->mode == KERNEL_MODE)
		{
			/*
			 * Kernel-mode init: switch CR3, set stack, jump.
			 * iretq with kernel CS/SS keeps us in ring 0.
			 */
			uint64_t kds = KERNEL_DATA_SEL;
			uint64_t kcs = KERNEL_CODE_SEL;
			__asm__ volatile(
				"cli\n"
				"mov %[cr3], %%rax\n"
				"mov %%rax, %%cr3\n"
				"mov %w[ds], %%ds\n"
				"mov %w[ds], %%es\n"
				"mov %w[ds], %%fs\n"
				"mov %w[ds], %%gs\n"
				"pushq %[ds]\n"
				"pushq %[rsp_val]\n"
				"pushq %[rflags]\n"
				"pushq %[cs_val]\n"
				"pushq %[rip_val]\n"
				"iretq\n"
				:
				: [cr3] "r"(next->task.cr3),
				  [rsp_val] "r"(next->task.rsp),
				  [rflags] "r"((uint64_t)RFLAGS_IF),
				  [rip_val] "r"(next->task.rip),
				  [ds] "r"(kds),
				  [cs_val] "r"(kcs)
				: "rax", "memory"
			);
		}
		else
		{
			arch_switch_to_user((arch_addr_t)next->task.rip,
					      (arch_addr_t)next->task.rsp);
		}
		panic("Returned from first context switch");
#else
		panic("First context switch not implemented for this architecture");
#endif
	}

	/* Normal context switch between processes.
	 * Save current process context (registers, stack, etc) and
	 * restore next process context. This is architecture-specific
	 * and handled by arch_context_switch().
	 */
	if (prev && next) 
	{
		/* Architecture-specific context switch.
		 * Saves prev->task and restores next->task.
		 * Does not return to this function - returns to next process.
		 */
		arch_context_switch(&prev->task, &next->task);
	}

	rr_irq_restore(irq_flags);
}
