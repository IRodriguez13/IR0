/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_frame.c
 * Description: x86-64 syscall/IRQ frame capture and kernel task stack setup.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/syscall_frame.h>
#include <ir0/arch_task.h>
#include <ir0/task_ops.h>
#include <ir0/errno.h>
#include <kernel/process.h>
#include <config.h>

void syscall_capture_frame_at_entry(struct process *p,
						 uint64_t *frame_base,
						 uint64_t rip_hw)
{
	syscall_user_frame_t *sf;

	if (!frame_base || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	/* frame_base[7] is the user RIP (rcx) at syscall entry. */
	sf->rip = frame_base[7];
	if (!sf->rip && rip_hw)
		sf->rip = rip_hw;
	sf->rflags = frame_base[6];
	sf->rsp = frame_base[8];
	sf->rbx = frame_base[0];
	sf->rbp = frame_base[1];
	sf->r12 = frame_base[2];
	sf->r13 = frame_base[3];
	sf->r14 = frame_base[4];
	sf->r15 = frame_base[5];
	sf->rdi = frame_base[-1];
	sf->rsi = frame_base[-2];
	sf->rdx = frame_base[-3];
	sf->r10 = frame_base[-4];
	sf->r8 = frame_base[-5];
	sf->r9 = frame_base[-6];
	sf->rax = frame_base[-7];
	sf->orig_rax = sf->rax;
	process_sync_task_user_ip_from_syscall_frame(p);
}

/*
 * @exit_frame is the syscall_64.asm stack from the r9 slot up: r9 r8 r10
 * rdx rsi rdi rbx rbp r12 r13 r14 r15 rflags rip rsp.
 */
void syscall_restore_exit_regs(struct process *p, uint64_t *exit_frame)
{
	const syscall_user_frame_t *sf;

	if (!exit_frame || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	exit_frame[0] = sf->r9;
	exit_frame[1] = sf->r8;
	exit_frame[2] = sf->r10;
	exit_frame[3] = sf->rdx;
	exit_frame[4] = sf->rsi;
	exit_frame[5] = sf->rdi;
	exit_frame[6] = sf->rbx;
	exit_frame[7] = sf->rbp;
	exit_frame[8] = sf->r12;
	exit_frame[9] = sf->r13;
	exit_frame[10] = sf->r14;
	exit_frame[11] = sf->r15;
	exit_frame[12] = sf->rflags;
	exit_frame[13] = sf->rip;
	exit_frame[14] = sf->rsp;
}

int exception_frame_is_user(const void *opaque_frame)
{
	const uint64_t *frame = opaque_frame;

	if (!frame)
		return 0;
	return ((frame[3] & 3U) == 3U) ? 1 : 0;
}
