/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_frame.c
 * Description: ARM64 syscall/IRQ frame capture (vectors.S exc_entry_frame).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/syscall_frame.h>
#include <ir0/arch_task.h>
#include <ir0/errno.h>
#include <kernel/process.h>
#include <stddef.h>
#include <asm/exc_frame.h>
#include <string.h>

static void arm64_copy_exc_gprs(arch_syscall_frame_t *sf, const uint64_t *frame)
{
	memcpy(&sf->x0, frame, 31 * sizeof(uint64_t));
}

void syscall_capture_frame_at_entry(struct process *p,
						 uint64_t *frame_base,
						 uint64_t rip_hw)
{
	arch_syscall_frame_t *sf;
	uint64_t elr;
	uint64_t spsr;
	uint64_t sp_el0;

	if (!frame_base || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	arm64_copy_exc_gprs(sf, frame_base);

	elr = frame_base[ARM64_EXC_IDX_ELR];
	spsr = frame_base[ARM64_EXC_IDX_SPSR];
	sp_el0 = frame_base[ARM64_EXC_IDX_SP];

	sf->elr = elr;
	if (!sf->elr && rip_hw)
		sf->elr = rip_hw;
	sf->spsr = spsr;
	sf->sp = sp_el0;
	p->syscall_frame_fresh = 1;
	process_sync_task_user_ip_from_syscall_frame(p);
}

void syscall_restore_exit_regs(struct process *p,
					    uint64_t *stack_r9_slot)
{
	const arch_syscall_frame_t *sf;

	if (!stack_r9_slot || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	memcpy(stack_r9_slot, &sf->x0, 31 * sizeof(uint64_t));

	/* @stack_r9_slot is the exception frame base; eret reloads these. */
	stack_r9_slot[ARM64_EXC_IDX_ELR] = sf->elr;
	stack_r9_slot[ARM64_EXC_IDX_SPSR] = sf->spsr;
	stack_r9_slot[ARM64_EXC_IDX_SP] = sf->sp;
}

int exception_frame_is_user(const void *opaque_frame)
{
	const uint64_t *frame = opaque_frame;

	/* SPSR_EL1.M[3:0] == 0 → EL0t (AArch64). */
	if (!frame)
		return 0;
	return (frame[ARM64_EXC_IDX_SPSR] & 0xfu) == 0u;
}
