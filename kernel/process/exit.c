/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exit.c
 * Description: process_exit (to zombie) and process_destroy (reaper teardown policy).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

/*
 * Teardown ownership policy
 * -------------------------
 * process_exit():
 *   - reparent children, release FDs, mark PROCESS_ZOMBIE
 *   - does NOT free page tables, kernel stack, or process_t
 * process_destroy() (reaper only):
 *   - release FDs again (idempotent clears), unmap user pages if owns_pml4,
 *     reclaim page tables, free mmap_list, saved_context, kernel stack, PML4
 *   - caller frees process_t after remove-from-list
 */
#if FASE40_D_AUDIT
void fase40_d_audit_reap_line(const char *stage, process_t *child,
				     pid_t parent_pid, int removed,
				     const char *tag)
{
	if (!child)
		return;

	serial_print("[FASE40_D_AUDIT][");
	serial_print(stage);
	serial_print("] child=");
	serial_print_hex32((uint32_t)child->task.pid);
	serial_print(" parent=");
	serial_print_hex32((uint32_t)parent_pid);
	serial_print(" removed=");
	serial_print_hex64((uint64_t)(int64_t)removed);
	serial_print(" tag=");
	serial_print(tag ? tag : "?");
	serial_print(" owns_pml4=");
	serial_print_hex64(child->owns_page_directory);
	serial_print(" pml4=");
	serial_print_hex64((uint64_t)(uintptr_t)child->page_directory);
	serial_print("\n");
}

void fase40_d_audit_destroy_done(process_t *p,
					const process_reclaim_stats_t *stats,
					uint64_t orphan_frames)
{
	size_t used_frames = 0;

	if (!p || !stats)
		return;

	pmm_stats(NULL, &used_frames, NULL);
	serial_print("[FASE40_D_AUDIT][UNMAP_DONE] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" mapped=");
	serial_print_hex64(stats->mapped_pages);
	serial_print(" freed=");
	serial_print_hex64(stats->freed_pages);
	serial_print(" missing=");
	serial_print_hex64(stats->missing_pages);
	serial_print(" orphan=");
	serial_print_hex64(orphan_frames);
	serial_print(" pmm_used=");
	serial_print_hex64((uint64_t)used_frames);
	serial_print("\n");
}
#endif

__attribute__((noreturn)) void process_exit(int code)
{
	process_t *dying = current_process;
	process_t *parent;
	size_t total_frames = 0;
	size_t used_frames = 0;
	uint64_t vmas = 0;

	if (!dying)
	{
		for (;;)
			arch_cpu_idle();
	}
	process_fase50_trace_proc("process_exit-entry", dying);
	dying->irq_frame_saved = 0;
	if (IR0_DEBUG_WAIT)
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] ZOMBIE_IRQ_SAVED_CLEARED\n");

	/* Before becoming a zombie:
	 * 1. Reparent all children to init (PID 1) to avoid orphaned processes
	 * 2. Clean up any zombie children we were waiting for
	 */
	process_reap_zombies(dying);
	process_reparent_children(dying);

	process_release_fds(dying, "EXIT_CLOSE");

#if IR0_DEBUG_PROC
	process_fase46_proc_log(dying, (int64_t)(uint32_t)code, "EXIT");
	process_fase44_list_checkpoint("exit-before");
	{
		fase_proc_audit_t *fa = fase_audit_get(dying, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_EXITING;
	}
	fase_audit_trace_pid(dying->task.pid, "EXIT");
	fase_audit_ref_emit(dying, "exit");
	process_fase43_proc_audit("exit-before");
#endif

	/* Mark as zombie */
	dying->state = PROCESS_ZOMBIE;
	if (dying->exit_signal == 0)
		dying->exit_code = code;
	else
		dying->exit_code = 0;
#if IR0_DEBUG_PROC
	if (dying->exit_signal > 0)
	{
		serial_print("[SIGTERM_AUDIT] process_exit pid=");
		serial_print_hex32((uint32_t)dying->task.pid);
		serial_print(" exit_signal=");
		serial_print_hex32((uint32_t)dying->exit_signal);
		serial_print(" wait_status=");
		serial_print_hex32((uint32_t)process_child_wait_status_word(dying));
		serial_print("\n");
	}
#endif
#if IR0_DEBUG_PROC
	{
		fase_proc_audit_t *fa = fase_audit_get(dying, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_ZOMBIE;
	}
	fase_audit_trace_pid(dying->task.pid, "ZOMBIE");
#endif
	fase_audit_note_proc_exited();
	fase_audit_note_proc_zombie();
#if IR0_DEBUG_PROC
	paging_fase42_checkpoint("exit-before", (int32_t)dying->task.pid);
#endif
	for (struct mmap_region *r = dying->mmap_list; r; r = r->next)
		vmas++;
	pmm_stats(&total_frames, &used_frames, NULL);
	if (IR0_DEBUG_PROC)
	{
		serial_print("[FASE41][EXIT] pid=");
		serial_print_hex32((uint32_t)dying->task.pid);
		serial_print(" vmas=");
		serial_print_hex64(vmas);
		serial_print(" used_frames=");
		serial_print_hex64((uint64_t)used_frames);
		serial_print(" total_frames=");
		serial_print_hex64((uint64_t)total_frames);
		serial_print("\n");
	}
#if CONFIG_DEBUG_FASE50
	serial_print("[PROCESS] exit pid=");
	serial_print_hex32((uint32_t)dying->task.pid);
	serial_print(" code=");
	serial_print_hex64((uint64_t)(uint32_t)code);
	serial_print("\n");
#endif

	process_fase43_proc_audit("exit-after");
#if IR0_DEBUG_PROC
	if (dying->task.pid == 1)
	{
		process_fase44_drain_zombie_children(1);
		process_fase43_live_proc_dump();
		process_fase44_live_summary("init-exit");
	}
	process_fase44_list_checkpoint("exit-after");
#endif

	/* Send SIGCHLD to parent process if it exists */
	if (dying->ppid > 0)
	{
		int parent_state_before = -1;

		parent = process_find_by_pid(dying->ppid);
		if (parent)
			parent_state_before = parent->state;
		if (parent && parent->state != PROCESS_ZOMBIE)
		{
			send_signal(parent->task.pid, SIGCHLD);
			if (parent->state == PROCESS_BLOCKED ||
			    parent->wait_blocked)
				process_wait_wake_blocked_parent(parent, dying);
		}
		else if (!parent || parent->state == PROCESS_ZOMBIE)
		{
			/* Parent is dead or zombie - reparent to init and send SIGCHLD to init */
			dying->ppid = 1;
			parent = process_find_by_pid(1);
			if (parent)
			{
				if (parent_state_before == -1)
					parent_state_before = parent->state;
				send_signal(parent->task.pid, SIGCHLD);
				if (parent->state == PROCESS_BLOCKED)
					process_wait_wake_blocked_parent(parent, dying);
			}
		}
		wait_exit_audit_process_exit(dying, parent, parent_state_before);
	}
	else
	{
		wait_exit_audit_process_exit(dying, NULL, -1);
	}

	/* Remove process from scheduler - it should no longer be scheduled.
	 * The process structure remains in memory as a zombie until reaped
	 * by the parent (via wait()), but it will not consume CPU time.
	 */
	sched_remove_process(dying);
	process_fase50_trace_proc("process_exit-before-schedule", dying);

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

	/* No runnable task: halt forever (must not sysret to exited user context). */
	for (;;)
		arch_cpu_idle();
}


/*
 * process_destroy - Release per-process resources before freeing a zombie struct.
 * Closes VFS and pipe handles, clears the FD table, and tears down user mappings
 * in this process's page directory (not necessarily the active CR3).
 */
void process_destroy(process_t *p)
{
	struct mmap_region *r;
	struct mmap_region *next;
	process_reclaim_stats_t reclaim_stats;
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;

	if (!p)
		return;
	memset(&reclaim_stats, 0, sizeof(reclaim_stats));

	ir0_console_purge_waiters_for_process(p);
	ir0_clock_wait_disarm(p);

	process_fase46_proc_log(p, -1, "DESTROY");
	fase_audit_ref_emit(p, "destroy");
	fase_audit_note_proc_destroyed();
	process_fase43_proc_audit("destroy-before");

#if CONFIG_DEBUG_FASE50
	serial_print("[PROCESS] destroy PID ");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" (fd cleanup)\n");
#endif

	process_release_fds(p, "DESTROY");

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][DESTROY] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" owns_pml4=");
		serial_print_hex64(p->owns_page_directory);
		serial_print(" pml4=");
		serial_print_hex64((uint64_t)(uintptr_t)p->page_directory);
		serial_print("\n");
	);

	/* Unmap all user pages in this process's PML4 (reaper may run under another CR3) */
	if (p->page_directory && p->owns_page_directory)
		process_unmap_user_pages_all(p->page_directory, &reclaim_stats);
	else
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][UNMAP_SKIP] pid=");
			serial_print_hex32((uint32_t)p->task.pid);
			serial_print(" owns_pml4=");
			serial_print_hex64(p->owns_page_directory);
			serial_print(" pml4=");
			serial_print_hex64((uint64_t)(uintptr_t)p->page_directory);
			serial_print("\n");
		);
	}
	if (p->page_directory && p->owns_page_directory)
		paging_reclaim_lower_half_tables(p->page_directory);

	/* Drop mmap bookkeeping nodes associated with this process. */
	r = p->mmap_list;
	while (r)
	{
		next = r->next;
		kfree(r);
		r = next;
	}
	p->mmap_list = NULL;

	if (p->saved_context)
	{
		kfree(p->saved_context);
		p->saved_context = NULL;
	}

	/* Release the private kernel stack (zombie is off-CPU; not in use). */
	process_kernel_stack_free(p);

	if (p->mode == KERNEL_MODE && p->stack_start &&
	    p->stack_start != INIT_DEBUG_STACK_BASE)
	{
		kfree((void *)p->stack_start);
		p->stack_start = 0;
		p->stack_size = 0;
	}

	if (p->page_directory && p->owns_page_directory)
	{
		paging_fase42_note_pml4_freed((uint64_t)(uintptr_t)p->page_directory);
		kfree_aligned(p->page_directory);
		p->page_directory = NULL;
		process_fase43_note_mm_destroyed();
	}

	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	FASE40_D_AUDIT_LOG(fase40_d_audit_destroy_done(p, &reclaim_stats, orphan_frames));
#if IR0_DEBUG_PMM
	serial_print("[FASE41][RECLAIM] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" pages_owned=");
	serial_print_hex64(reclaim_stats.mapped_pages);
	serial_print(" pages_freed=");
	serial_print_hex64(reclaim_stats.freed_pages);
	serial_print(" missing_pages=");
	serial_print_hex64(reclaim_stats.missing_pages);
	serial_print(" delta=");
	serial_print_hex64(reclaim_stats.mapped_pages - reclaim_stats.freed_pages);
	serial_print("\n");
	serial_print("[FASE41][PT] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" pdpt_present=");
	serial_print_hex64(reclaim_stats.pdpt_present);
	serial_print(" pd_present=");
	serial_print_hex64(reclaim_stats.pd_present);
	serial_print(" pt_present=");
	serial_print_hex64(reclaim_stats.pt_present);
	serial_print(" leaf_present=");
	serial_print_hex64(reclaim_stats.leaf_present);
	serial_print(" pdpt_freed=");
	serial_print_hex64(reclaim_stats.pdpt_freed);
	serial_print(" pd_freed=");
	serial_print_hex64(reclaim_stats.pd_freed);
	serial_print(" pt_freed=");
	serial_print_hex64(reclaim_stats.pt_freed);
	serial_print(" leaf_freed=");
	serial_print_hex64(reclaim_stats.leaf_freed);
	serial_print("\n");
	serial_print("[FASE41][PMM_AUDIT] orphan_frames=");
	serial_print_hex64(orphan_frames);
	serial_print(" double_free=");
	serial_print_hex64(double_free);
	serial_print(" alive_owner_missing=");
	serial_print_hex64(alive_owner_missing);
	serial_print("\n");
#endif
	paging_fase42_checkpoint("destroy-after", (int32_t)p->task.pid);
	{
		fase_proc_audit_t *fa = fase_audit_get(p, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_DESTROYED;
	}
	fase_audit_unbind(p);
	process_fase43_proc_audit("destroy-after");
}

