/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: context.c
 * Description: x86-64 __switch_to and kernel-entry stack (TSS) hooks.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/context_backend.h>
#include <ir0/task.h>
#include <ir0/process.h>
#include <asm/process_ctx_invariant.h>
#include <asm/context.h>
#include <ir0/ktm/klog.h>
#include <ir0/ktm/fault.h>
#include <ir0/arch_mm.h>

#include "tss.h"

extern uint64_t kernel_syscall_stack_top;

void context_backend_inject_kernel_return_fault(task_t *task)
{
	process_t *proc = task ? task_to_process(task) : NULL;

	if (!task || !proc || proc->mode != USER_MODE ||
	    !KTM_FAULT_HIT("sched.class_b_arm_window"))
		return;

	/* KTM creates the ISA-specific Class-B frame for generic recovery tests. */
	task_set_kernel_segments(task);
	task_set_ip(task, IR0_USER_RIP_LO + 0x1000ULL);
	if (!process_rip_in_user_range(task_get_sp(task)))
		task_set_sp(task, 0x00007FFFFFF0ULL);
	process_syscall_set_ip(proc, task_get_ip(task));
	process_syscall_set_sp(proc, task_get_sp(task));
	process_syscall_set_flags(proc, task_get_flags(task) | 2ULL);
	klog_info("CTX", "CLASSIFY CLASS_B_FAULT_INJECT");
}

void set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	if (!proc || !proc->kstack_top)
		return;

	kernel_syscall_stack_top = proc->kstack_top;
	tss_set_rsp0(proc->kstack_top);
}

void __switch_to(task_t *prev, task_t *next)
{
	switch_context_x64(prev, next);
}
