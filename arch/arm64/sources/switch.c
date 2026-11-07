/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: switch.c
 * Description: ARM64 task switch: every switch is kernel to kernel
 *              (Linux cpu_switch_to). A task that has never run starts at its
 *              kernel entry or at the ret-to-user trampoline on its own stack.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_cpu.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <sched/task.h>

extern void arm64_cpu_switch(void *prev_ctx, void *next_ctx);
extern void arm64_cpu_switch_mm(void *prev_ctx, void *next_ctx,
				uint64_t next_ttbr);

/*
 * First run of a user task (fork child, new image): we are on the task's own
 * kernel stack with current_process already pointing at it (switch_to()
 * order). Return to EL0 through the common user-entry path (Linux
 * ret_from_fork).
 */
static void arm64_ret_to_user(void)
{
	process_t *p = current_process;

	if (!p)
		panic("arm64_ret_to_user: no current process");
	switch_to_user_task(&p->task);
}

static void arm64_prepare_first_run(task_t *next)
{
	process_t *proc = task_to_process(next);

	if (proc && proc->mode == USER_MODE)
	{
		if (!proc->kstack_top)
			panic("arm64: user task without kernel stack");
		next->arch.kctx[ARM64_KCTX_LR] =
			(uint64_t)(uintptr_t)arm64_ret_to_user;
		next->arch.kctx[ARM64_KCTX_SP] = proc->kstack_top;
	}
	else
	{
		/* Kernel thread: entry in elr_el1, stack in sp_el0 (task_set_*). */
		next->arch.kctx[ARM64_KCTX_LR] = next->arch.elr_el1;
		next->arch.kctx[ARM64_KCTX_SP] = next->arch.sp_el0;
	}
	next->arch.kctx_valid = 1;
}

void switch_context_arm64(task_t *prev, task_t *next)
{
	static uint64_t discard_ctx[13];
	void *prev_ctx;
	uint64_t next_ttbr;

	if (!next)
		return;
	if (!next->arch.kctx_valid)
		arm64_prepare_first_run(next);

	if (prev)
	{
		prev->arch.kctx_valid = 1;
		prev_ctx = (void *)prev->arch.kctx;
	}
	else
		prev_ctx = (void *)discard_ctx;

	next_ttbr = next->arch.ttbr0_el1;
	if (next_ttbr)
		arm64_cpu_switch_mm(prev_ctx, (void *)next->arch.kctx, next_ttbr);
	else
		arm64_cpu_switch(prev_ctx, (void *)next->arch.kctx);
}
