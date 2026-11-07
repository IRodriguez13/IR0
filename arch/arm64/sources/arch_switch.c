/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_switch.c
 * Description: ARM64 switch_to — TTBR0, SP_EL0/SP_EL1, TPIDR_EL0 TLS, EL1 switch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_switch.h>
#include <ir0/arch_task.h>
#include <ir0/arch_cpu.h>
#include <ir0/context_backend.h>
#include <ir0/process.h>
#include <stdint.h>
#include <config.h>

/*
 * Must match exc_entry / exc_entry_frame in arch/arm64/asm/entry/vectors.S
 * (sub sp, sp, #256 before saving the trap frame).
 */
#define ARM64_EXC_FRAME_RESERVE 256U

extern void switch_context_arm64(task_t *prev, task_t *next);

void set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	/*
	 * Do not MSR SP_EL1 at EL1h. That register is the live stack
	 * (SPSel=1); writing it clobbers LR and RET-to-0 (ARM64_SYNC_OTHER).
	 * Switching to SPSel=0 to reach the inactive bank hangs this QEMU
	 * virt bring-up before the first EL0 drop. Keep the boot SP_EL1
	 * for lower-EL vectors; cpu_local_set_kernel_stack still records
	 * kstack_top. ARM DDI 0487: SPSel selects the current SP.
	 */
	(void)proc;
}

void switch_save_user_rsp(struct process *prev)
{
	process_t *proc = (process_t *)prev;
	uint64_t sp_el0;

	if (!proc)
		return;

	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	proc->saved_user_rsp = sp_el0;
}

void arch_switch_to(task_t *prev, task_t *next)
{
	process_t *next_proc;

	if (!next)
		return;

	next_proc = task_to_process(next);

	if (next_proc)
	{
		/* Always program SP_EL0 — skipping when saved_user_rsp==0 left
		 * a stale value from the previous task (Bugbot).
		 */
		{
			uint64_t sp_el0 = next_proc->saved_user_rsp;

			if (!sp_el0)
				sp_el0 = task_get_sp(next);
			__asm__ volatile("msr sp_el0, %0" :: "r"(sp_el0)
					 : "memory");
		}

		/*
		 * First user entry (and later resumes whose SPSR is EL0t): x86
		 * switch_context_x64 iretqs when CS.RPL==3. ARM cooperative
		 * switch only RETs at EL1, so drop through switch_to_user.
		 * Blocked-syscall kernel_ret still uses switch_context_arm64.
		 */
		if (next_proc->mode == USER_MODE &&
		    task_return_state_is_user(next) &&
		    !next_proc->kernel_syscall_sleep &&
		    !next_proc->want_kernel_ret)
		{
			switch_to_user_task(next);
			return;
		}
	}

	switch_context_arm64(prev, next);
}

int context_backend_checkpoint(task_t *prev)
{
	(void)prev;
	return 0;
}

void context_backend_prepare_user_frame(struct process *proc, task_t *task)
{
	(void)proc;
	(void)task;
}

uint64_t context_backend_user_return_value(const struct process *proc,
					       const task_t *task)
{
	const process_t *resume_proc = (const process_t *)proc;

	(void)task;
	return resume_proc ? resume_proc->syscall_resume_rax : 0;
}

void context_backend_trace_user_frame_resume(struct process *prev,
						     struct process *next,
						     task_t *task)
{
	(void)prev;
	(void)next;
	(void)task;
}

void context_backend_prepare_kernel_resume(task_t *task)
{
	(void)task;
}

void context_backend_inject_kernel_return_fault(task_t *task)
{
	(void)task;
}
