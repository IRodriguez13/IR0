/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exit.c
 * Description: process exit policy and zombie publication.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/ktm/checkpoint.h>

/*
 * Teardown ownership policy
 * -------------------------
 * process_exit():
 *   - reparent children, release FDs, mark PROCESS_ZOMBIE
 *   - does NOT free page tables, kernel stack, or process_t
 * process_destroy() (reaper only):
 *   - release FDs again (idempotent clears), unmap user pages if owns_pml4,
 *     reclaim translation tables, free mmap_list, saved_context, kernel stack
 *   - caller frees process_t after remove-from-list
 */

void process_pre_zombie_teardown(process_t *dying)
{
	if (!dying)
		return;

	dying->irq_frame_saved = 0;
	process_signal_defer_catchable_clear(dying);
	process_signal_last_delivered_clear(dying);
	process_kernel_sleep_interrupted_clear(dying);
	process_reap_zombies(dying);
	process_reparent_children(dying);
	process_resources_release_on_exit(dying);
}

__attribute__((noreturn)) void process_exit(int code)
{
	process_t *dying = current_process;

	if (!dying)
	{
		for (;;)
			cpu_idle();
	}
	process_pre_zombie_teardown(dying);
	if (IR0_DEBUG_WAIT)
		klog_debug("WAIT", "CLASSIFY ZOMBIE_IRQ_SAVED_CLEARED");

	/* Mark as zombie */
	process_lifecycle_publish_exit(dying, code);
	KTM_CHECKPOINT(KTM_CP_PROCESS_EXIT);
#if IR0_DEBUG_PROC
	if (dying->exit_signal > 0)
	{
		klog_debug_fmt("SIGNAL", "[SIGTERM_AUDIT] process_exit pid=%x exit_signal=%x wait_status=%x", (unsigned)((uint32_t)dying->task.pid), (unsigned)((uint32_t)dying->exit_signal), (unsigned)((uint32_t)process_child_wait_status_word(dying)));
	}
	paging_ir0_mm_checkpoint("exit-before", (int32_t)dying->task.pid);
#endif

	process_notify_parent_of_exit(dying);

	/* Remove process from scheduler - it should no longer be scheduled.
	 * The process structure remains in memory as a zombie until reaped
	 * by the parent (via wait()), but it will not consume CPU time.
	 */
	sched_remove_process(dying);

	/*
	 * kmain keeps the kernel idle task off the RR queue while PID 1 runs;
	 * enqueue it again when a user process exits so sched has a fallback.
	 */
	{
		process_t *p;

		for (p = process_list; p; p = p->next)
		{
			if (p->mode == KERNEL_MODE && p->state != PROCESS_ZOMBIE &&
			    strncmp(p->comm, "idle", sizeof(p->comm)) == 0)
			{
				sched_add_process(p);
				break;
			}
		}
	}

	/* Switch to another process - this will never return to this code.
	 * The zombie process remains in memory with its exit code for the
	 * parent to retrieve via wait().
	 */
	sched_schedule_next();

	/* No runnable task: halt forever; never resume an exited user context. */
	for (;;)
		cpu_idle();
}
