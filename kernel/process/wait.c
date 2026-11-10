/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: wait.c
 * Description: wait4, zombie reaping and parent blocking policy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/ktm/checkpoint.h>

int process_reap_detach_zombie(process_t *child)
{
	int removed;

	removed = process_lifecycle_prepare_reap(child);
	if (removed != 0)
		return removed;

	removed = process_remove_from_list(child);
	if (removed != 0)
		return removed;
	process_lifecycle_publish_reaped(child);
	return 0;
}

void process_reap_zombie_child(process_t *child)
{
	if (process_reap_detach_zombie(child) != 0)
		return;

	KTM_CHECKPOINT(KTM_CP_PROCESS_REAP);
	process_destroy(child);
	kfree(child);
}

/**
 * process_reap_zombies - Automatically reap zombie children of a process
 * @parent: Parent process
 *
 * When a process exits, clean up any zombie children that were waiting for it.
 * Also used by init to periodically clean up zombies.
 */
void process_reap_zombies(process_t *parent)
{
	process_t *child;
	process_t *next;
	
	if (!parent)
		return;
	
	child = process_list;
	
	while (child)
	{
		next = child->next;
		
		/* Check if this is a zombie child of the parent */
		if (child->ppid == parent->task.pid && child->state == PROCESS_ZOMBIE)
		{
#if DEBUG_PROCESS
			klog_debug_fmt("KERN", "[PROCESS] Auto-reaping zombie child PID %x", (unsigned)((uint32_t)child->task.pid));
#endif
			process_reap_zombie_child(child);
		}
		
		child = next;
	}
}

static int process_wait_pid_matches_child(pid_t wait_pid,
					  const process_t *parent,
					  const process_t *child)
{
	return process_lifecycle_child_matches(parent, child, wait_pid);
}

int process_has_zombie_child(const process_t *parent)
{
	process_t *p;

	if (!parent)
		return 0;

	for (p = process_list; p; p = p->next)
	{
		if (p->ppid == parent->task.pid && p->state == PROCESS_ZOMBIE)
			return 1;
	}
	return 0;
}

int process_wait_child_matches_blocked_target(const process_t *parent,
					    pid_t child_pid)
{
	process_t *child;

	if (!parent || child_pid <= 0 || !process_wait_blocked(parent))
		return 0;

	child = process_find_by_pid(child_pid);
	if (!child || child->ppid != parent->task.pid)
		return 0;
	return process_wait_pid_matches_child(process_wait_target_pid(parent), parent,
					      child);
}

int process_child_wait_status_word(const process_t *child)
{
	return process_lifecycle_wait_status(child);
}

int process_wait_prepare_child_wake(const process_t *parent,
				    const process_t *child, int *status_word)
{
	if (!parent || !child || !status_word)
		return -EINVAL;
	if (!process_wait_child_matches_blocked_target(parent, child->task.pid))
		return -ECHILD;
	if (!process_is_zombie(child))
		return -EAGAIN;
	*status_word = process_child_wait_status_word(child);
	return 0;
}

void process_wait_wake_blocked_parent(process_t *parent, process_t *child)
{
	int status_val;

	if (process_wait_prepare_child_wake(parent, child, &status_val) != 0)
		return;

	/* The woken wait4 reaps @child itself on its own stack. */
	process_set_sched_state(parent, PROCESS_READY);
	sched_add(parent);
	sched_promote(parent);
}

/* First zombie child of @parent matching the wait4 @pid selector, or NULL. */
static process_t *wait_find_matching_zombie(process_t *parent, pid_t pid)
{
	process_t *p;

	for (p = process_list; p; p = p->next)
	{
		if (p->ppid != parent->task.pid)
			continue;
		if (!process_wait_pid_matches_child(pid, parent, p))
			continue;
		if (p->state == PROCESS_ZOMBIE)
			return p;
	}
	return NULL;
}

int process_wait(pid_t pid, int *status, int options)
{
	process_t *p;
	int found_child;
	process_t *zombie;
	uint64_t irq_flags;
	/*
	 * wait4 contract (D1.17 / Linux waitpid):
	 *   pid > 0  — that child.
	 *   pid -1   — any child of this process.
	 *   pid  0   — any child in the caller's process group.
	 *   pid < -1 — any child whose pgid is -pid.
	 *   WNOHANG  — 0 if no matching zombie yet (never ECHILD when matches exist).
	 *   ECHILD   — no matching child relationship at all.
	 * User-mode block stores wait_target_pid; wake/resume must not complete or
	 * reap a different child (see process_wait_wake_blocked_parent / wait-resume).
	 */
	if (!current_process) {
		klog_debug("KERN", "[ERROR] process_wait called without current process context\n");
		return -ESRCH;
	}

	/*
	 * wait_options for USER_MODE is seeded in sys_wait4 from pt_regs (rdx).
	 * Do not copy from the stack parameter here — it may already be clobbered.
	 */

	for (;;) {
		const int 		active_opts = (current_process->mode == USER_MODE)
			? process_wait_options(current_process)
			: options;
		found_child = 0;
		zombie = NULL;
		irq_flags = process_irq_save();

		for (p = process_list; p; p = p->next) {
			if (p->ppid != current_process->task.pid)
				continue;
			if (!process_wait_pid_matches_child(pid, current_process, p))
				continue;

			found_child = 1;
			if (p->state == PROCESS_ZOMBIE) {
				zombie = p;
				break;
			}
		}

		if (zombie) {
			int status_val;
			pid_t reaped_pid;

			if (status && current_process->mode == USER_MODE &&
			    !access_ok(status, sizeof(int)))
			{
				process_irq_restore(irq_flags);
				return -EFAULT;
			}

			status_val = process_child_wait_status_word(zombie);
			reaped_pid = zombie->task.pid;
			process_irq_restore(irq_flags);

			process_reap_zombie_child(zombie);
			wait_exit_audit_process_wait_reap(reaped_pid, status_val, status);

			if (status)
			{
				if (current_process->mode == KERNEL_MODE)
					*status = status_val;
				else if (copy_to_user(status, &status_val, sizeof(int)) != 0)
					return -EFAULT;
			}
			process_wait_state_clear(current_process);
			/*
			 * wait(2) consumed this child exit (Linux waitpid). Drop stale
			 * SIGCHLD pending when no zombies remain — otherwise
			 * sigprocmask(SIG_UNBLOCK) after waitpid runs the musl handler
			 * and corrupts the supervisor syscall frame (rip=user stack).
			 */
			if (!wait_find_matching_zombie(current_process, (pid_t)-1))
				current_process->signal_pending &= ~SIGNAL_MASK(SIGCHLD);

			return reaped_pid;
		}

		if (!found_child)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] path=echild parent=%x target=%x ret=ECHILD\n", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid));
			}
			process_wait_state_clear(current_process);
			return -ECHILD;
		}

		if (active_opts & WNOHANG)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] path=wnohang_alive parent=%x target=%x ret=0 status_write=no\n", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid));
			}
			process_wait_state_clear(current_process);
			return 0;
		}

		/*
		 * Arm wait contract before dropping irq: child exit wake is ignored
		 * until wait_blocked is set (process_wait_wake_blocked_parent).
		 */
		if (current_process->mode == USER_MODE)
		{
			wait_exit_audit_process_wait_block(pid, status);
			process_wait_state_arm(current_process, pid, active_opts, status);
			wait_exit_audit_classify_user_frame("parent-after-wait-arm",
							    current_process);
		}
		else
			process_wait_state_arm(current_process, pid, active_opts, NULL);

		zombie = NULL;
		for (p = process_list; p; p = p->next)
		{
			if (p->ppid != current_process->task.pid)
				continue;
			if (!process_wait_pid_matches_child(pid, current_process, p))
				continue;
			if (p->state == PROCESS_ZOMBIE)
			{
				zombie = p;
				break;
			}
		}
		/*
		 * prepare_to_wait (Linux kernel/sched/wait.c): publish BLOCKED
		 * under the same irq-save that process_wait_wake_blocked_parent
		 * takes, before releasing it. Marking BLOCKED after the restore
		 * left a window where a child exit set READY and this store
		 * clobbered it back to BLOCKED — wait4 then slept with a zombie
		 * already reaped (pipeline completes, shell never returns).
		 */
		if (!zombie)
			process_set_sched_state(current_process, PROCESS_BLOCKED);
		process_irq_restore(irq_flags);
		if (zombie)
			continue;
		/*
		 * wait_event_interruptible: a caught signal ends the wait, and
		 * the syscall exit restarts it under SA_RESTART or fails it with
		 * EINTR. Checked after BLOCKED is published, so a signal sent
		 * in between has already set READY.
		 */
		if (signal_pending(current_process))
		{
			process_set_sched_state(current_process, PROCESS_READY);
			process_wait_state_clear(current_process);
			return -ERESTARTSYS;
		}

		while (current_process->state == PROCESS_BLOCKED)
		{
			/*
			 * wait_event ordering: the condition, not just the
			 * sleep state, ends the wait. The loop above rescans
			 * for the zombie and the signal.
			 */
			if (wait_find_matching_zombie(current_process, pid) ||
			    signal_pending(current_process))
			{
				/*
				 * Unlike the sigsuspend/pause loops, the reap
				 * path here does not republish READY, so the
				 * caller would return still marked BLOCKED.
				 */
				process_set_sched_state(current_process,
							PROCESS_READY);
				break;
			}
			ir0_clock_wait_service_runqueue();
			if (current_process->state != PROCESS_BLOCKED)
				break;
		}
	}
}
