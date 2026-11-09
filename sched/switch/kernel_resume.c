/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel_resume.c
 * Description: Settle a user task's blocked-syscall state before it
 *              continues in the kernel.
 *
 * Every switch resumes the incoming task's kernel continuation on its own
 * kernel stack (Linux switch_to()); the task returns to userspace through
 * its normal syscall or exception exit. What remains here is bookkeeping
 * for that continuation plus the Class B recovery (a kernel return state
 * paired with a user instruction pointer), which only reports through the
 * switch diag hook.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "switch_internal.h"
#include <ir0/context_backend.h>
#include <ir0/process.h>

/* A wait has not produced the child that completes it yet. */
static int waits_for_child(const process_t *proc)
{
	return (process_wait_blocked(proc) || process_wait_target_pid(proc) != 0) &&
	       process_wait_resume_child_pid(proc) <= 0;
}

static int ip_is_in_user_stack(const process_t *proc, uint64_t ip)
{
	uint64_t stack_start = process_stack_start(proc);
	uint64_t stack_size = process_stack_size(proc);

	return stack_size != 0 && ip >= stack_start &&
	       ip - stack_start < stack_size;
}

/*
 * The task continues in the kernel: a staged user frame is stale, and a
 * waiter whose saved IP is not user text must re-enter its syscall.
 */
static void settle_blocked_state(process_t *proc, task_t *next)
{
	if (waits_for_child(proc))
	{
		if (!mm_user_va_ok((uintptr_t)task_get_ip(next), 1))
			process_arm_kernel_syscall_sleep(proc);
		if (!process_wait_blocked(proc))
			proc->irq_frame_saved = 0;
		return;
	}
	proc->irq_frame_saved = 0;
}

/*
 * The backend may have repaired its saved frame before this point. Re-arm
 * the kernel continuation only after that, without inspecting an ISA frame.
 */
static void finalize_kernel_resume(process_t *proc, task_t *next)
{
	if (process_wait_target_pid(proc) != 0 &&
	    process_wait_resume_child_pid(proc) <= 0 &&
	    !mm_user_va_ok((uintptr_t)task_get_ip(next), 1))
		process_arm_kernel_syscall_sleep(proc);

	if (proc->want_kernel_ret &&
	    !mm_user_va_ok((uintptr_t)task_get_ip(next), 1))
		process_arm_kernel_syscall_sleep(proc);
}

/*
 * Class B: a kernel return state with a user instruction pointer cannot be
 * resumed. Rebuild the user return from the syscall entry frame when it is
 * sound, otherwise demote to the user segments.
 */
static void repair_kernel_return_state(process_t *proc, task_t *next)
{
	const syscall_user_frame_t *frame;
	int frame_addresses_valid;

	if (!process_task_kernel_return_state_bad(next))
		return;

	frame = &proc->syscall_frame;
	frame_addresses_valid =
		mm_user_va_ok((uintptr_t)process_syscall_ip(proc), 1) &&
		mm_user_va_ok((uintptr_t)process_syscall_sp(proc), 1);

	if (waits_for_child(proc))
	{
		switch_trace(SWITCH_TRACE_KERNEL_RETURN_WAIT_DEMOTE);
		proc->irq_frame_saved = 0;
		process_restore_user_task_segments(proc);
		if (proc->syscall_frame_fresh && frame_addresses_valid)
			process_apply_syscall_frame_to_task(next, frame,
						    proc->syscall_resume_rax);
		return;
	}

	if (frame_addresses_valid &&
	    !ip_is_in_user_stack(proc, process_syscall_ip(proc)))
	{
		switch_trace(SWITCH_TRACE_KERNEL_RETURN_FRAME_REPAIR);
		process_apply_syscall_frame_to_task(next, frame,
					    context_backend_user_return_value(proc, next));
		if (process_signal_enter_pending(proc) &&
		    process_saved_context_present(proc))
			process_signal_enter_pending_clear(proc);
		return;
	}

	switch_trace(SWITCH_TRACE_KERNEL_RETURN_UNREPAIRED_DEMOTE);
	proc->irq_frame_saved = 0;
	process_restore_user_task_segments(proc);
	if (proc->syscall_frame_fresh && frame_addresses_valid)
		process_apply_syscall_frame_to_task(next, frame,
					    proc->syscall_resume_rax);
}

void context_prepare_kernel_resume(task_t *next)
{
	process_t *proc = next ? task_to_process(next) : NULL;

	if (!proc || proc->mode != USER_MODE)
		return;

	settle_blocked_state(proc, next);
	context_backend_prepare_kernel_resume(next);
	finalize_kernel_resume(proc, next);
	context_backend_inject_kernel_return_fault(next);
	if (IR0_CLASS_B_REPAIR)
		repair_kernel_return_state(proc, next);
}
