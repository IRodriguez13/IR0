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
#include <ir0/paging.h>
#include <ir0/process.h>

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

	context_prepare_user_frame_resume(next);
	context_backend_prepare_user_frame(proc, next);
	context_finish_user_frame_resume(proc);
	switch_to_user_task(next);
	return 1;
}
