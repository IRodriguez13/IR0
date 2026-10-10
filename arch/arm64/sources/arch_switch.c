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

extern void switch_context_arm64(task_t *prev, task_t *next);

void set_current_kernel_stack(struct process *p)
{
	process_t *proc = (process_t *)p;

	if (!proc || !proc->kstack_top)
		return;

	/*
	 * Exception entry from EL0 uses SP_EL1. Cooperative EL1 switch keeps
	 * the running SP; this primes the banked stack for the next EL0 drop.
	 */
	__asm__ volatile("msr sp_el1, %0" :: "r"(proc->kstack_top) : "memory");
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
	}

	switch_context_arm64(prev, next);
}

void prepare_task_user_iretq(struct process *proc)
{
	(void)proc;
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
