/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: context_user_resume.c
 * Description: Portable blocked-syscall user-frame resume state machine.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/context.h>
#include <ir0/context_backend.h>
#include <ir0/ktm/deferred.h>
#include <ir0/klog.h>
#include <ir0/mm.h>
#include <ir0/paging.h>
#include <ir0/process.h>

static int context_ip_is_in_user_stack(const process_t *proc, uint64_t ip)
{
	uint64_t stack_start;
	uint64_t stack_size;

	if (!proc)
		return 0;
	stack_start = process_stack_start(proc);
	stack_size = process_stack_size(proc);
	return stack_size != 0 && ip >= stack_start &&
	       ip - stack_start < stack_size;
}

/*
 * Reconcile only process and scheduler state.  Whether a raw saved frame is
 * safe, and how it becomes a hardware return, remain ISA backend work.
 */
enum context_resume_route context_prepare_resume_route(task_t *next)
{
	process_t *proc;
	enum context_resume_route route;

	if (!next)
		return CONTEXT_RESUME_SWITCH;
	proc = task_to_process(next);
	if (!proc || proc->mode != USER_MODE)
		return CONTEXT_RESUME_SWITCH;

	if (process_context_waits_for_child(proc))
	{
		if (!mm_user_va_ok((uintptr_t)task_get_ip(next), 1))
			process_arm_kernel_syscall_sleep(proc);
		if (!process_wait_blocked(proc))
		{
			proc->irq_frame_saved = 0;
			proc->coop_resched_resume = 0;
		}
		return CONTEXT_RESUME_KERNEL;
	}

	route = process_context_resume_route(proc);
	if (!proc->irq_frame_saved || route != CONTEXT_RESUME_KERNEL)
		return route;

	/* A stale frame must continue in the kernel, never be returned to EL0/ring 3. */
	proc->irq_frame_saved = 0;
	proc->coop_resched_resume = 0;
	return CONTEXT_RESUME_KERNEL;
}

/*
 * The backend may have repaired its saved frame before this point.  Re-arm
 * generic kernel continuation only after that validation, without inspecting
 * an ISA return frame or segment representation.
 */
void context_finalize_kernel_resume(task_t *next)
{
	process_t *proc;

	if (!next)
		return;
	proc = task_to_process(next);
	if (!proc)
		return;

	if (process_wait_target_pid(proc) != 0 &&
	    process_wait_resume_child_pid(proc) <= 0 &&
	    !proc->coop_resched_resume &&
	    !mm_user_va_ok((uintptr_t)task_get_ip(next), 1))
		process_arm_kernel_syscall_sleep(proc);

	if (proc->want_kernel_ret &&
	    !mm_user_va_ok((uintptr_t)task_get_ip(next), 1))
		process_arm_kernel_syscall_sleep(proc);
}

/*
 * A task with a kernel return state and a user instruction pointer cannot be
 * resumed directly.  This is process-state recovery; segment/register layout
 * remains hidden behind the task and backend facades.
 */
void context_repair_kernel_return_state(task_t *next)
{
	process_t *proc;
	const syscall_user_frame_t *frame;
	int frame_addresses_valid;

	if (!next)
		return;
	proc = task_to_process(next);
	if (!proc || proc->mode != USER_MODE || proc->coop_resched_resume ||
	    !process_task_kernel_return_state_bad(next))
		return;

	frame = &proc->syscall_frame;
	frame_addresses_valid =
		mm_user_va_ok((uintptr_t)process_syscall_ip(proc), 1) &&
		mm_user_va_ok((uintptr_t)process_syscall_sp(proc), 1);

	if (process_context_waits_for_child(proc))
	{
		klog_info("CTX", "CLASSIFY KERNEL_RETURN_WAIT_DEMOTE");
		proc->irq_frame_saved = 0;
		process_restore_user_task_segments(proc);
		if (proc->syscall_frame_fresh && frame_addresses_valid)
			process_apply_syscall_frame_to_task(next, frame,
						    proc->syscall_resume_rax);
		return;
	}

	if (frame_addresses_valid &&
	    !context_ip_is_in_user_stack(proc, process_syscall_ip(proc)))
	{
		klog_info("CTX", "CLASSIFY KERNEL_RETURN_FRAME_REPAIR");
		process_apply_syscall_frame_to_task(next, frame,
					    context_backend_user_return_value(proc, next));
		if (process_signal_enter_pending(proc) &&
		    process_saved_context_present(proc))
			process_signal_enter_pending_clear(proc);
		return;
	}

	klog_info("CTX", "CLASSIFY KERNEL_RETURN_UNREPAIRED_DEMOTE");
	proc->irq_frame_saved = 0;
	process_restore_user_task_segments(proc);
	if (proc->syscall_frame_fresh && frame_addresses_valid)
		process_apply_syscall_frame_to_task(next, frame,
					    proc->syscall_resume_rax);
}

/*
 * No ISA state is inspected or synthesized here: the frame is applied through
 * the task facade, while the backend remains responsible for validating it
 * and performing the eventual privilege return.
 */
void context_prepare_user_frame_resume(task_t *next)
{
	process_t *proc;
	int coop_resume;
	pid_t resume_child;
	uint64_t resume_rax;

	if (!next)
		return;
	proc = task_to_process(next);
	if (!proc)
		return;

	if (task_mm_root(next))
		paging_activate_address_space(task_mm_root(next));

	coop_resume = proc->coop_resched_resume != 0;
	resume_child = process_wait_resume_child_pid(proc);
	if (!coop_resume)
	{
		if (resume_child <= 0)
			resume_child = (pid_t)proc->syscall_resume_rax;
		process_reap_zombie_on_wait_resume(proc, resume_child);
	}

	resume_rax = proc->syscall_resume_rax;
	if (!coop_resume && process_wait_blocked(proc) &&
	    process_wait_resume_child_pid(proc) > 0)
		resume_rax = (uint64_t)process_wait_resume_child_pid(proc);
	process_apply_syscall_frame_to_task(next, &proc->syscall_frame, resume_rax);
}

void context_finish_user_frame_resume(struct process *proc)
{
	process_t *resume_proc = (process_t *)proc;

	if (!resume_proc)
		return;

	process_wait_status_ptr_set(resume_proc, NULL);
	process_wait_blocked_clear(resume_proc);
	process_wait_target_pid_set(resume_proc, 0);
	process_wait_options_set(resume_proc, 0);
	process_wait_resume_child_pid_set(resume_proc, 0);
	resume_proc->irq_frame_saved = 0;
	resume_proc->coop_resched_resume = 0;
	resume_proc->kernel_syscall_sleep = 0;
	process_kernel_sleep_interrupted_clear(resume_proc);
}

/*
 * The scheduler owns the order of a direct user-frame return.  Backends only
 * save raw execution state, repair their own frame representation and perform
 * the non-returning privilege transition through switch_to_user_task().
 */
int context_resume_user_frame(task_t *prev, task_t *next)
{
	process_t *proc;

	if (!next)
		return 1;
	proc = task_to_process(next);
	if (!proc)
		return 1;

	/* A resumed cooperative caller must unwind its old switch invocation. */
	if (proc->coop_resched_resume && prev &&
	    context_backend_checkpoint(prev) != 0)
		return 1;

	/*
	 * Record only the transfer that will actually reach userspace.  A resumed
	 * cooperative checkpoint unwinds above and must not produce a second gate
	 * event for the old switch invocation.
	 */
	ktm_deferred_record(KTM_DEFERRED_RESUME_GATE, (uint32_t)next->pid,
			    (uint64_t)proc->kernel_syscall_sleep,
			    (uint64_t)process_wait_blocked(proc) |
				    ((uint64_t)(uint32_t)process_wait_resume_child_pid(proc) << 8),
			    proc->syscall_resume_rax);

	context_prepare_user_frame_resume(next);
	context_backend_prepare_user_frame(proc, next);
	context_finish_user_frame_resume(proc);
	switch_to_user_task(next);
	return 1;
}
