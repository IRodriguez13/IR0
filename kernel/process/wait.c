/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: wait.c
 * Description: wait4/reap/reparent and wait-exit audit; zombie until process_destroy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"

void wait_exit_audit_classify_user_frame(const char *tag, process_t *p)
{
#if !IR0_DEBUG_WAIT
	(void)tag;
	(void)p;
	return;
#else
	uint64_t rip;
	uint64_t rsp;
	uint16_t cs;
	uint16_t ss;

	if (!p)
		return;

	rip = p->task.rip;
	rsp = p->task.rsp;
	cs = p->task.cs;
	ss = p->task.ss;

	serial_print("[WAIT_EXIT_AUDIT][FRAME] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" comm=");
	serial_print(p->comm);
	serial_print(" rip=");
	serial_print_hex64(rip);
	serial_print(" rsp=");
	serial_print_hex64(rsp);
	serial_print(" cs=");
	serial_print_hex64((uint64_t)cs);
	serial_print(" ss=");
	serial_print_hex64((uint64_t)ss);
	serial_print(" rflags=");
	serial_print_hex64(p->task.rflags);
	serial_print(" rax=");
	serial_print_hex64(p->task.rax);
	serial_print(" cr3=");
	serial_print_hex64(p->task.cr3);
	serial_print(" irq_saved=");
	serial_print_hex64((uint64_t)p->irq_frame_saved);
	serial_print("\n");

	if (rip < 0x00400000ULL || rip > 0x00007FFFFFFFFFFFULL)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RIP\n");
	}
	if (rsp < 0x00400000ULL || rsp > 0x00007FFFFFFFFFFFULL)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RSP\n");
	}
	if (cs != (uint16_t)USER_CODE_SEL || ss != (uint16_t)USER_DATA_SEL)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_CS_SS\n");
	}
#endif
}

void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
                                         int parent_state_before)
{
#if !IR0_DEBUG_WAIT
	(void)dying;
	(void)parent;
	(void)parent_state_before;
	return;
#else
	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_pid=");
	serial_print_hex32((uint32_t)dying->task.pid);
	serial_print(" child_state_before=RUNNING child_state_after=ZOMBIE parent_pid=");
	serial_print_hex32(parent ? (uint32_t)parent->task.pid : 0);
	serial_print(" parent_state_before=");
	serial_print_hex64((uint64_t)(unsigned int)parent_state_before);
	serial_print(" parent_state_after=");
	serial_print_hex64(parent ? (uint64_t)(unsigned int)parent->state : 0);
	serial_print(" parent_woken=");
	serial_print_hex64((uint64_t)(parent && parent_state_before == PROCESS_BLOCKED &&
	                             parent->state == PROCESS_READY ? 1 : 0));
	serial_print("\n");

	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_frees address_space=0 page_directory=0 "
	             "task_struct=0 kernel_stack=0 (zombie until reap)\n");
	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_closes fd_table=1 cwd=0 vfs_mm=0\n");

	if (parent)
	{
		serial_print("[WAIT_EXIT_AUDIT][process_exit] parent_mm=");
		serial_print_hex64((uint64_t)(uintptr_t)parent->page_directory);
		serial_print(" parent_cr3=");
		serial_print_hex64(parent->task.cr3);
		serial_print(" parent_files=");
		serial_print_hex64((uint64_t)(uintptr_t)parent->fd_table);
		serial_print(" parent_irq_saved=");
		serial_print_hex64((uint64_t)parent->irq_frame_saved);
		serial_print("\n");
		wait_exit_audit_classify_user_frame("parent-at-child-exit", parent);
	}

	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_irq_saved=");
	serial_print_hex64((uint64_t)dying->irq_frame_saved);
	serial_print("\n");
	if (dying->irq_frame_saved)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] EXIT_FREED_PARENT_RESOURCE "
		             "note=child_irq_saved_stale_may_misroute_resume\n");
	}
#endif
}

void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status)
{
#if !IR0_DEBUG_WAIT
	(void)wait_pid;
	(void)status;
	return;
#else
	if (!current_process)
		return;

	serial_print("[WAIT_EXIT_AUDIT][process_wait] action=block parent_pid=");
	serial_print_hex32((uint32_t)current_process->task.pid);
	serial_print(" wait_pid=");
	serial_print_hex32((uint32_t)wait_pid);
	serial_print(" status_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)status);
	serial_print(" expected_rax_after_wake=child_pid\n");
	wait_exit_audit_classify_user_frame("parent-before-wait-sleep", current_process);
	serial_print("[WAIT_EXIT_AUDIT][process_wait] syscall_frame rip=");
	serial_print_hex64(current_process->syscall_frame.rip);
	serial_print(" rsp=");
	serial_print_hex64(current_process->syscall_frame.rsp);
	serial_print("\n");
#endif
}

void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val, int *status)
{
#if !IR0_DEBUG_WAIT
	(void)reaped_pid;
	(void)status_val;
	(void)status;
	return;
#else
	if (!current_process)
		return;

	serial_print("[WAIT_EXIT_AUDIT][process_wait] action=reap parent_pid=");
	serial_print_hex32((uint32_t)current_process->task.pid);
	serial_print(" reaped_pid=");
	serial_print_hex32((uint32_t)reaped_pid);
	serial_print(" status_val=");
	serial_print_hex64((uint64_t)(unsigned int)status_val);
	serial_print(" status_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)status);
	serial_print(" return_rax=");
	serial_print_hex64((uint64_t)(unsigned int)reaped_pid);
	serial_print("\n");
	wait_exit_audit_classify_user_frame("parent-after-reap", current_process);
#endif
}

process_t *process_find_by_pid(pid_t pid)
{
	process_t *proc;
	uint64_t irq_flags = process_irq_save();

	proc = process_list;
	
	while (proc)
	{
		if (proc->task.pid == pid)
		{
			process_irq_restore(irq_flags);
			return proc;
		}
		proc = proc->next;
	}
	
	process_irq_restore(irq_flags);
	return NULL; /* Not found */
}

int process_remove_from_list(process_t *target)
{
	process_t *scan;
	process_t *prev;
	uint64_t irq_flags;

	if (!target)
		return -EINVAL;

	irq_flags = process_irq_save();
	prev = NULL;
	scan = process_list;

	while (scan)
	{
		if (scan == target)
		{
			if (prev)
				prev->next = scan->next;
			else
				process_list = scan->next;
			scan->next = NULL;
			process_irq_restore(irq_flags);
			return 0;
		}
		prev = scan;
		scan = scan->next;
	}

	process_irq_restore(irq_flags);
	return -ENOENT;
}

/**
 * process_reparent_children - Reparent all children to init (PID 1)
 * @dying_parent: Process that is about to exit
 *
 * When a parent process dies, all its children become orphans.
 * This function reparents them to init (PID 1) so they don't become zombies.
 */
void process_reparent_children(process_t *dying_parent)
{
	process_t *child;
	process_t *init;
	
	if (!dying_parent)
		return;
	
	/* Find init process (PID 1) */
	init = process_find_by_pid(1);
	if (!init)
	{
		/* No init process - this is a critical system error */
		serial_print("[CRITICAL] Init process (PID 1) not found during reparenting\n");
		serial_print("[CRITICAL] System integrity compromised - orphaned processes detected\n");
		/* Continue execution but log the critical error */
		return;
	}
	
	/* Find all children of dying parent */
	child = process_list;
	while (child)
	{
		if (child->ppid == dying_parent->task.pid)
		{
			fase_proc_audit_t *fa = fase_audit_get(child, 0);
			uint8_t audit_st = fa ? fa->fase44_audit_state : 0;

			child->ppid = 1;
			fase_audit_note_reparent();
			fase_audit_destroy_audit(child, dying_parent->task.pid,
					     audit_st, audit_st, 0, "reparent");
#if DEBUG_PROCESS
			serial_print("[PROCESS] Reparented child PID ");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print(" to init (PID 1)\n");
#endif
		}
		child = child->next;
	}
	process_fase44_list_checkpoint("reparent-after");
}

void process_reap_zombie_child(process_t *child)
{
	int removed;

	if (!child)
		return;

	removed = process_remove_from_list(child);
	FASE40_D_AUDIT_LOG(fase40_d_audit_reap_line("REAP_CHILD", child, 0, removed,
						    "reap_zombie_child"));
	if (removed != 0)
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][REAP_SKIP_DESTROY] child=");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print(" reason=remove_from_list err=");
			serial_print_hex64((uint64_t)(int64_t)removed);
			serial_print("\n");
		);
		return;
	}
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
			serial_print("[PROCESS] Auto-reaping zombie child PID ");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print("\n");
#endif
			fase_audit_note_reap_event();
			process_fase43_proc_audit("reap-zombie");
			fase_audit_reap_zombie(child, parent->task.pid, "reap-zombie");
		}
		
		child = next;
	}
	process_fase44_list_checkpoint("reap-zombie-after");
}

int process_wait_child_matches_blocked_target(const process_t *parent,
					    pid_t child_pid)
{
	pid_t target;
	int any_child;

	if (!parent || child_pid <= 0 || !parent->wait_blocked)
		return 0;

	target = parent->wait_target_pid;
	any_child = (target == (pid_t)-1 || target == 0);
	if (!any_child && child_pid != target)
		return 0;

	return 1;
}

int process_child_wait_status_word(const process_t *child)
{
	if (!child)
		return 0;
	if (child->exit_signal > 0)
		return child->exit_signal & 0x7f;
	return (child->exit_code & 0xff) << 8;
}

void process_wait_wake_blocked_parent(process_t *parent, process_t *child)
{
	int status_val;
	int *status_ptr;
	int copy_ret;

	if (!parent || !child || !parent->wait_blocked)
		return;

	if (!process_wait_child_matches_blocked_target(parent, child->task.pid))
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][REAP_SKIP_NOT_TARGET] parent=");
			serial_print_hex32((uint32_t)parent->task.pid);
			serial_print(" target=");
			serial_print_hex32((uint32_t)parent->wait_target_pid);
			serial_print(" candidate=");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print("\n");
		);
		return;
	}

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][CHILD_EXIT] parent=");
		serial_print_hex32((uint32_t)parent->task.pid);
		serial_print(" target=");
		serial_print_hex32((uint32_t)parent->wait_target_pid);
		serial_print(" child=");
		serial_print_hex32((uint32_t)child->task.pid);
		serial_print("\n");
	);

	if (!parent->irq_frame_saved)
	{
		/*
		 * Blocked via process_arm_kernel_syscall_sleep: keep kernel CS/SS so
		 * switch_context_x64 resumes with kernel_ret into process_wait, not
		 * user iretq with stale task.rax (placeholder 0 at block time).
		 */
		parent->state = PROCESS_READY;
		sched_add_process(parent);
		sched_promote_process(parent);
		return;
	}

	status_val = process_child_wait_status_word(child);
	status_ptr = parent->wait_status_ptr;
	if (!status_ptr)
		status_ptr = (int *)(uintptr_t)parent->syscall_frame.rsi;

	copy_ret = -1;
	if (status_ptr && parent->page_directory &&
	    process_validate_userspace_buffer(status_ptr, sizeof(int)) == 0)
	{
		copy_ret = copy_to_user_region_in_directory(parent->page_directory,
							    (uintptr_t)status_ptr,
							    &status_val,
							    sizeof(int));
	}

	fase51_dbg_wait_wake((uint32_t)parent->task.pid, (uint32_t)child->task.pid,
			     status_ptr, status_val, copy_ret);
#if IR0_DEBUG_PROC
	serial_print("[SIGTERM_AUDIT] wait_wake parent=");
	serial_print_hex32((uint32_t)parent->task.pid);
	serial_print(" child=");
	serial_print_hex32((uint32_t)child->task.pid);
	serial_print(" status=");
	serial_print_hex32((uint32_t)status_val);
	serial_print(" rax=");
	serial_print_hex32((uint32_t)child->task.pid);
	serial_print("\n");
#endif
	parent->wait_resume_child_pid = child->task.pid;
	parent->syscall_resume_rax = (uint64_t)child->task.pid;
	parent->state = PROCESS_READY;
	sched_add_process(parent);
	sched_promote_process(parent);
}

void process_reap_zombie_on_wait_resume(process_t *parent, pid_t child_pid)
{
	process_t *child;
	size_t used_frames_before = 0;
	size_t used_frames_after = 0;

	if (!parent || child_pid <= 0)
		return;

	if (!process_wait_child_matches_blocked_target(parent, child_pid))
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][REAP_SKIP_NOT_TARGET] parent=");
			serial_print_hex32((uint32_t)parent->task.pid);
			serial_print(" target=");
			serial_print_hex32((uint32_t)parent->wait_target_pid);
			serial_print(" resume_child=");
			serial_print_hex32((uint32_t)child_pid);
			serial_print("\n");
		);
		return;
	}

	child = process_find_by_pid(child_pid);
	if (!child || child->state != PROCESS_ZOMBIE ||
	    child->ppid != parent->task.pid)
		return;

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][REAP_MATCH] parent=");
		serial_print_hex32((uint32_t)parent->task.pid);
		serial_print(" child=");
		serial_print_hex32((uint32_t)child_pid);
		serial_print("\n");
	);

	pmm_stats(NULL, &used_frames_before, NULL);
	process_fase43_proc_audit("wait-resume-before-reap");
	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][WAIT_RESUME_REAP] parent=");
		serial_print_hex32((uint32_t)parent->task.pid);
		serial_print(" child=");
		serial_print_hex32((uint32_t)child_pid);
		serial_print(" resume_rax=");
		serial_print_hex64(parent->syscall_resume_rax);
		serial_print("\n");
	);
	fase_audit_reap_zombie(child, parent->task.pid, "wait-resume");
	pmm_stats(NULL, &used_frames_after, NULL);
	process_fase44_list_checkpoint("wait-resume-after");
	process_fase43_proc_audit("wait-resume-after-reap");
	if (IR0_DEBUG_PROC)
	{
		serial_print("[FASE41][WAIT_REAP] parent_pid=");
		serial_print_hex32((uint32_t)parent->task.pid);
		serial_print(" child_pid=");
		serial_print_hex32((uint32_t)child_pid);
		serial_print(" used_before=");
		serial_print_hex64((uint64_t)used_frames_before);
		serial_print(" used_after=");
		serial_print_hex64((uint64_t)used_frames_after);
		serial_print(" delta=");
		if (used_frames_after >= used_frames_before)
			serial_print_hex64((uint64_t)(used_frames_after - used_frames_before));
		else
			serial_print_hex64((uint64_t)(used_frames_before - used_frames_after));
		serial_print(" sign=");
		serial_print(used_frames_after <= used_frames_before ? "-" : "+");
		serial_print("\n");
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] ");
		serial_print(used_frames_after <= used_frames_before ?
			     "PMM_RECLAIM_ON_WAIT_OK" : "PMM_RECLAIM_ON_WAIT_PARTIAL");
		serial_print("\n");
	}
	paging_fase42_checkpoint("wait-resume-after", (int32_t)parent->task.pid);
}

int process_wait(pid_t pid, int *status, int options)
{
	process_t *p;
	int found_child;
	process_t *zombie;
	uint64_t irq_flags;
	process_fase50_trace_proc("process_wait-entry", current_process);
	process_fase43_proc_audit("wait-before");
	process_fase44_list_checkpoint("wait-before");
	/*
	 * wait4 contract (D1.17):
	 *   pid > 0  — block until that child is ZOMBIE, then reap only that pid.
	 *   pid -1/0 — any child of this process (groups not implemented).
	 *   WNOHANG  — 0 if no matching zombie yet (never ECHILD when children exist).
	 *   ECHILD   — no matching child relationship at all.
	 * User-mode block stores wait_target_pid; wake/resume must not complete or
	 * reap a different child (see process_wait_wake_blocked_parent / wait-resume).
	 */
	const int any_child = (pid == (pid_t)-1 || pid == 0);

	if (!current_process) {
		serial_print("[ERROR] process_wait called without current process context\n");
		return -ESRCH;
	}

	/*
	 * wait_options for USER_MODE is seeded in sys_wait4 from pt_regs (rdx).
	 * Do not copy from the stack parameter here — it may already be clobbered.
	 */

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][WAIT_BEGIN] parent=");
		serial_print_hex32((uint32_t)current_process->task.pid);
		serial_print(" target=");
		serial_print_hex32((uint32_t)pid);
		serial_print(" options=");
		serial_print_hex32((uint32_t)(current_process->mode == USER_MODE
					      ? current_process->wait_options
					      : (uint32_t)options));
		serial_print("\n");
	);

	for (;;) {
		const int active_opts = (current_process->mode == USER_MODE)
			? current_process->wait_options
			: options;
		found_child = 0;
		zombie = NULL;
		irq_flags = process_irq_save();

		for (p = process_list; p; p = p->next) {
			if (p->ppid != current_process->task.pid)
				continue;
			if (!any_child && p->task.pid != pid)
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
			process_fase50_trace_proc("process_wait-found-zombie", zombie);

			if (status &&
			    process_validate_userspace_buffer(status, sizeof(int)) != 0)
			{
				process_irq_restore(irq_flags);
				return -EFAULT;
			}

			status_val = process_child_wait_status_word(zombie);
			reaped_pid = zombie->task.pid;
			process_irq_restore(irq_flags);

			FASE40_D_AUDIT_LOG(
				serial_print("[FASE40_D_AUDIT][WAIT_REAP] parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" child=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print(" tag=wait\n");
			);
			fase_audit_reap_zombie(zombie, current_process->task.pid, "wait");
			process_fase50_trace_proc("process_wait-after-reap", current_process);
			wait_exit_audit_process_wait_reap(reaped_pid, status_val, status);
			process_fase44_list_checkpoint("wait-after");
			process_fase43_proc_audit("wait-reap");

			if (status)
			{
				if (current_process->mode == KERNEL_MODE)
					*status = status_val;
				else if (copy_to_user(status, &status_val, sizeof(int)) != 0)
					return -EFAULT;
			}
			current_process->wait_status_ptr = NULL;
			current_process->irq_frame_saved = 0;
			current_process->wait_blocked = 0;
			current_process->wait_target_pid = 0;
			current_process->wait_options = 0;
			current_process->wait_resume_child_pid = 0;
			current_process->syscall_resume_rax = 0;
			current_process->coop_resched_resume = 0;
			current_process->task.rax = (uint64_t)(uint32_t)reaped_pid;

			if (IR0_DEBUG_PROC)
			{
				serial_print("[FASE41][WAIT] pid=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" child=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print(" status=");
				serial_print_hex64((uint64_t)(uint32_t)status_val);
				serial_print("\n");
			}

			FASE40_D_AUDIT_LOG(
				serial_print("[FASE40_D_AUDIT][WAIT_RETURN] parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" pid=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print(" status=");
				serial_print_hex32((uint32_t)status_val);
				serial_print(" rax=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print("\n");
			);

			return reaped_pid;
		}

		if (!found_child)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				serial_print("[WAIT4_WNOHANG_AUDIT] path=echild parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" target=");
				serial_print_hex32((uint32_t)pid);
				serial_print(" ret=ECHILD\n");
			}
			if (IR0_DEBUG_PROC)
			{
				serial_print("[FASE41][WAIT] pid=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" ret=ECHILD\n");
			}
			process_reset_blocked_syscall_state(current_process);
			return -ECHILD;
		}

		if (active_opts & WNOHANG)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				serial_print("[WAIT4_WNOHANG_AUDIT] path=wnohang_alive parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" target=");
				serial_print_hex32((uint32_t)pid);
				serial_print(" ret=0 status_write=no\n");
			}
			if (IR0_DEBUG_PROC)
			{
				serial_print("[FASE41][WAIT] pid=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" ret=WNOHANG\n");
			}
			process_reset_blocked_syscall_state(current_process);
			return 0;
		}

		/*
		 * Arm wait contract before dropping irq: child exit wake is ignored
		 * until wait_blocked is set (process_wait_wake_blocked_parent).
		 */
		if (current_process->mode == USER_MODE)
		{
			wait_exit_audit_process_wait_block(pid, status);
			current_process->wait_status_ptr = status;
			current_process->wait_blocked = 1;
			current_process->wait_target_pid = pid;
			current_process->wait_options = active_opts;
			current_process->wait_resume_child_pid = 0;
			current_process->coop_resched_resume = 0;
			current_process->syscall_resume_rax = 0;
			current_process->task.rax = 0;
			process_arm_blocked_syscall_resume(current_process, 0);
			process_arm_kernel_syscall_sleep(current_process);
			wait_exit_audit_classify_user_frame("parent-after-wait-arm",
							    current_process);
		}
		else
		{
			current_process->wait_blocked = 1;
			current_process->wait_target_pid = pid;
			current_process->wait_options = active_opts;
			current_process->wait_resume_child_pid = 0;
		}

		zombie = NULL;
		for (p = process_list; p; p = p->next)
		{
			if (p->ppid != current_process->task.pid)
				continue;
			if (!any_child && p->task.pid != pid)
				continue;
			if (p->state == PROCESS_ZOMBIE)
			{
				zombie = p;
				break;
			}
		}
		process_irq_restore(irq_flags);
		if (zombie)
			continue;

		/*
		 * Child exit may have woken us after irq_restore; do not clobber
		 * READY with BLOCKED (missed wake → stuck with a zombie).
		 */
		if (current_process->state == PROCESS_READY)
			continue;

		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][WAIT_BLOCK] parent=");
			serial_print_hex32((uint32_t)current_process->task.pid);
			serial_print(" target=");
			serial_print_hex32((uint32_t)pid);
			serial_print("\n");
		);

		current_process->state = PROCESS_BLOCKED;
		while (current_process->state == PROCESS_BLOCKED)
		{
			ir0_clock_wait_service_runqueue();
			if (current_process->state != PROCESS_BLOCKED)
				break;
		}
		/*
		 * Stay on kernel CS until process_wait returns; syscall_dispatch
		 * restores user segments at sysret. Restoring here while still
		 * inside process_wait lets switch_context user-iret with the
		 * block-time rax=0 placeholder (wait4_block_reap flake).
		 */
	}
}



