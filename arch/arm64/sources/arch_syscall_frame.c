/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_syscall_frame.c
 * Description: ARM64 stubs for syscall/IRQ frame capture (EL0 path TBD).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_syscall_frame.h>
#include <ir0/arch_task.h>
#include <ir0/errno.h>
#include <stddef.h>

void arch_process_capture_syscall_frame_at_entry(struct process *p,
						 uint64_t *frame_base,
						 uint64_t rip_hw)
{
	(void)p;
	(void)frame_base;
	(void)rip_hw;
}

void arch_process_syscall_restore_exit_regs(struct process *p,
					    uint64_t *stack_r9_slot)
{
	(void)p;
	(void)stack_r9_slot;
}

int arch_irq_frame_is_user(const uint64_t *iretq_frame)
{
	(void)iretq_frame;
	return 0;
}

void arch_process_save_user_context_from_irq(uint64_t *gpr_stack)
{
	(void)gpr_stack;
}

int arch_task_setup_kernel_stack(task_t *task, void *stack_base,
				 size_t stack_size, void (*entry)(void *))
{
	if (!task || !stack_base || !entry || stack_size < 64)
		return -EINVAL;

	/* Minimal EL1 stack pointer; full frame layout when switch_early lands. */
	task_set_sp(task, (uint64_t)((uintptr_t)stack_base + stack_size));
	task_set_ip(task, (uint64_t)(uintptr_t)entry);
	return 0;
}
