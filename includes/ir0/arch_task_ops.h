/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_task_ops.h
 * Description: ISA-specific bulk task context operations and sigcontext transfer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/signals.h>
#include <ir0/task.h>

/*
 * Linux pt_regs subset mirrored for arch backends — layout matches
 * process.h syscall_user_frame_t without including process.h here.
 */
typedef struct arch_task_syscall_frame
{
	uint64_t rip;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rdx;
	uint64_t r10;
	uint64_t r8;
	uint64_t r9;
} arch_task_syscall_frame_t;

void arch_task_apply_syscall_frame(task_t *task,
				   const arch_task_syscall_frame_t *sf,
				   uint64_t rax);
void arch_task_sync_syscall_soft_mirror(task_t *task,
					const arch_task_syscall_frame_t *sf);
void arch_task_save_irq_user_frame(task_t *task, const uint64_t *iretq_frame);
void arch_task_apply_kernel_segments(task_t *task);
void arch_task_apply_user_segments(task_t *task);
uint64_t *arch_task_retval_slot_addr(task_t *task);

void arch_task_load_sigcontext(task_t *t, const struct sigcontext *ctx);
void arch_task_store_sigcontext(struct sigcontext *ctx, const task_t *t);
