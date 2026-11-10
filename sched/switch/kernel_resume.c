/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel_resume.c
 * Description: Class B recovery before a user task's kernel
 *              continuation runs.
 *
 * Every switch resumes the incoming task's kernel continuation on its own
 * kernel stack (Linux switch_to()); the task returns to userspace through
 * its normal syscall or exception exit. What remains here is the Class B
 * recovery (a kernel return state paired with a user instruction pointer),
 * which only reports through the switch diag hook.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "switch_internal.h"
#include <ir0/context_backend.h>
#include <ir0/process.h>

/*
 * Class B: a kernel return state with a user instruction pointer cannot be
 * resumed. Rebuild the user return from the syscall frame when it is sound,
 * otherwise demote to the user segments.
 */
static void repair_kernel_return_state(process_t *proc, task_t *next)
{
	const syscall_user_frame_t *frame = &proc->syscall_frame;

	if (!process_task_kernel_return_state_bad(next))
		return;

	if (mm_user_va_ok((uintptr_t)process_syscall_ip(proc), 1) &&
	    mm_user_va_ok((uintptr_t)process_syscall_sp(proc), 1))
	{
		switch_trace(SWITCH_TRACE_KERNEL_RETURN_FRAME_REPAIR);
		process_apply_syscall_frame_to_task(next, frame,
						    syscall_frame_return(frame));
		return;
	}

	switch_trace(SWITCH_TRACE_KERNEL_RETURN_UNREPAIRED_DEMOTE);
	process_restore_user_task_segments(proc);
}

void context_prepare_kernel_resume(task_t *next)
{
	process_t *proc = next ? task_to_process(next) : NULL;

	if (!proc || proc->mode != USER_MODE)
		return;

	context_backend_inject_kernel_return_fault(next);
	if (IR0_CLASS_B_REPAIR)
		repair_kernel_return_state(proc, next);
}
