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
#include <ir0/futex.h>
#include <ir0/copy_user.h>
#include <ir0/ktm/checkpoint.h>

#if CONFIG_ENABLE_NETWORKING
void tcp_wire_on_process_exit(uint32_t pid);
#endif

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

static void process_exit_robust_list_hook(process_t *dying)
{
	process_exit_robust_list(dying);
}

static void process_exit_clear_child_tid_hook(process_t *dying)
{
	int zero = 0;
	int *tidptr;

	if (!dying->set_tid_ptr)
		return;

	tidptr = dying->set_tid_ptr;
	dying->set_tid_ptr = NULL;
	if (dying->mode == USER_MODE)
	{
		if (copy_to_user_in_mm(process_pgd(dying),
				       dying == current_process, tidptr, &zero,
				       sizeof(zero)) == 0)
			(void)ir0_futex_wake(tidptr, 1);
	}
	else if (process_validate_userspace_buffer(tidptr, sizeof(zero)) == 0)
	{
		(void)copy_to_user(tidptr, &zero, sizeof(zero));
		(void)ir0_futex_wake(tidptr, 1);
	}
}

void process_pre_zombie_teardown(process_t *dying)
{
	if (!dying)
		return;

	dying->irq_frame_saved = 0;
	process_signal_defer_catchable_clear(dying);
	process_signal_last_delivered_clear(dying);
	process_kernel_sleep_interrupted_clear(dying);
#if CONFIG_ENABLE_NETWORKING
	tcp_wire_on_process_exit((uint32_t)dying->task.pid);
#endif
	process_exit_robust_list_hook(dying);
	process_exit_clear_child_tid_hook(dying);

	process_reap_zombies(dying);
	process_reparent_children(dying);

	/*
	 * Drop wait registrations before zombification so wake paths cannot
	 * retain or reschedule a process whose resources are being released.
	 */
	ir0_console_purge_waiters_for_process(dying);
	pipe_purge_waiters_for_process(dying);
	ipc_purge_waiters_for_process(dying);
	if (dying->fork_pending_child)
		process_fork_abort_pending_on_exit(dying);
	process_mm_release_on_exit(dying);
	if (dying->pgid > 1)
		ir0_console_clear_fg_pgid((int32_t)dying->pgid,
					 (int32_t)dying->task.pid);
	if ((pid_t)dying->task.pid == dying->sid)
	{
		/*
		 * Next getty must see a clean cooked tty. Purge waiters above
		 * drop the dying reader; without a session flush the new
		 * username read sits blocked (prompt printed, keys ignored).
		 */
		if (ir0_console_has_ctty_for_sid((int32_t)dying->sid))
		{
			ir0_console_flush_input_session();
			ir0_console_reset_cooked_echo();
		}
		ir0_console_clear_ctty_session((int32_t)dying->sid);
	}

	process_release_fds(dying, "EXIT_CLOSE");
}

void process_notify_parent_of_exit(process_t *dying)
{
	process_t *parent = NULL;
	int parent_state_before = -1;

	if (!dying)
		return;

	if (dying->ppid > 0)
	{
		parent = process_find_by_pid(dying->ppid);
		if (parent)
			parent_state_before = parent->state;
		if (parent && parent->state != PROCESS_ZOMBIE)
		{
			send_signal(parent->task.pid, SIGCHLD);
			if (parent->state == PROCESS_BLOCKED ||
			    process_wait_blocked(parent))
				process_wait_wake_blocked_parent(parent, dying);
		}
		else
		{
			/*
			 * A dead or missing parent cannot reap this zombie. Move the
			 * relationship to init before publishing SIGCHLD.
			 */
			dying->ppid = 1;
			parent = process_find_by_pid(1);
			if (parent)
			{
				if (parent_state_before == -1)
					parent_state_before = parent->state;
				send_signal(parent->task.pid, SIGCHLD);
				if (parent->state == PROCESS_BLOCKED ||
				    process_wait_blocked(parent))
					process_wait_wake_blocked_parent(parent, dying);
			}
			else
			{
				dying->ppid = 0;
			}
		}
	}

	wait_exit_audit_process_exit(dying, parent, parent_state_before);
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
	process_mark_zombie(dying);
	KTM_CHECKPOINT(KTM_CP_PROCESS_EXIT);
	if (dying->exit_signal == 0)
		dying->exit_code = code;
	else
		dying->exit_code = 0;
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

	/* No runnable task: halt forever (must not sysret to exited user context). */
	for (;;)
		cpu_idle();
}


/*
 * process_destroy - Release per-process resources before freeing a zombie struct.
 * Closes VFS and pipe handles, clears the FD table, and tears down user
 * mappings in this process's page directory (not the active hardware root).
 */
void process_destroy(process_t *p)
{
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;

	if (!p)
		return;

	/*
	 * Spawn-fail and reap-without-exit must drop vfork slots too.
	 * process_exit already ran this; a second call is a no-op.
	 */
	process_mm_release_on_exit(p);

	ir0_console_purge_waiters_for_process(p);
	pipe_purge_waiters_for_process(p);
	ir0_clock_wait_disarm(p);

	process_release_fds(p, "DESTROY");

	if (p->files)
	{
		files_put(p->files);
		p->files = NULL;
	}

	/*
	 * Address space teardown via mm refcount. Unmap the private kstack
	 * while the process PML4 is still alive (high VA pages live there).
	 */
	{
		uint64_t kstack = 0;

		if (p->mode == KERNEL_MODE)
			kstack = process_stack_start(p);

		process_kernel_stack_free(p);

		if (p->mm)
		{
			mm_put(p->mm);
			p->mm = NULL;
		}

		if (p->mode == KERNEL_MODE && kstack &&
		    kstack != INIT_DEBUG_STACK_BASE)
			kfree((void *)(uintptr_t)kstack);
	}

	process_saved_context_clear(p);
	process_saved_environ_clear(p);
	process_saved_cmdline_clear(p);

	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	paging_ir0_mm_checkpoint("destroy-after", (int32_t)p->task.pid);
}
