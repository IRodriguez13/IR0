/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: context.c
 * Description: ARM64 switch_to — TTBR0, SP_EL0/SP_EL1, TPIDR_EL0 TLS, EL1 switch.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/context_backend.h>
#include <ir0/process.h>
#include <ir0/switch.h>
#include <ir0/task.h>

#include "switch_early.h"

/*
 * Must match exc_entry / exc_entry_frame in arch/arm64/asm/entry/vectors.S
 * (sub sp, sp, #256 before saving the trap frame).
 */
#define ARM64_EXC_FRAME_RESERVE 256U

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

/*
 * Every switch is kernel to kernel on the task's own stack (Linux
 * cpu_switch_to). Returning to EL0 always goes through the task's exception
 * frame or, on first run, the ret-to-user trampoline (switch.c), so the
 * outgoing task keeps its kernel continuation. SP_EL0 is restored from the
 * exception frame (asm/exc_frame.h), not programmed here.
 */
void context_backend_switch(task_t *prev, task_t *next)
{
	if (!next)
		return;
	switch_context_arm64(prev, next);
}

uint64_t context_backend_user_return_value(const struct process *proc,
					       const task_t *task)
{
	const process_t *resume_proc = (const process_t *)proc;

	(void)task;
	return resume_proc ? resume_proc->syscall_resume_rax : 0;
}

void context_backend_prepare_kernel_resume(task_t *task)
{
	(void)task;
}

void context_backend_inject_kernel_return_fault(task_t *task)
{
	(void)task;
}
