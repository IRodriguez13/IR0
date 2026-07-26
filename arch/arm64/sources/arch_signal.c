/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_signal.c
 * Description: ARM64 exception-frame ↔ Linux aarch64 sigcontext delivery.
 *
 * Frame layout matches vectors.S exc_entry_frame (x0@0 … x30@240).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_signal.h>
#include <ir0/arch_task.h>
#include <ir0/signals.h>
#include <stdint.h>
#include <string.h>

uint64_t arch_sigcontext_ip(const struct sigcontext *ctx)
{
	return ctx ? ctx->pc : 0;
}

void arch_signal_fill_sigcontext_from_irq_frame(struct sigcontext *ctx,
						const uint64_t *frame)
{
	uint64_t far;
	uint64_t elr;
	uint64_t spsr;
	uint64_t sp_el0;
	unsigned i;

	if (!ctx || !frame)
		return;

	memset(ctx, 0, sizeof(*ctx));
	for (i = 0; i < 31; i++)
		ctx->regs[i] = frame[i];

	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	ctx->fault_address = far;
	ctx->pc = elr;
	ctx->pstate = spsr;
	ctx->sp = sp_el0;
}

uint64_t arch_irq_frame_sp(const uint64_t *frame)
{
	uint64_t sp_el0;

	(void)frame;
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));
	return sp_el0;
}

void arch_signal_redirect_irq_frame(uint64_t *frame, void *handler, int sig,
				    uint64_t new_rsp, uint64_t info_addr,
				    uint64_t uctx_addr, int sa_siginfo)
{
	uint64_t elr = (uint64_t)(uintptr_t)handler;

	if (!frame || !handler)
		return;

	/* AAPCS64: x0=signum, x1=siginfo*, x2=ucontext* for SA_SIGINFO. */
	frame[0] = (uint64_t)(uint32_t)sig;
	if (sa_siginfo)
	{
		frame[1] = info_addr;
		frame[2] = uctx_addr;
	}
	else
	{
		frame[1] = 0;
		frame[2] = 0;
	}

	__asm__ volatile("msr elr_el1, %0" :: "r"(elr) : "memory");
	__asm__ volatile("msr sp_el0, %0" :: "r"(new_rsp) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

void arch_signal_prepare_task_handler(task_t *t, void *handler, int sig,
				      uint64_t frame_sp)
{
	if (!t || !handler)
		return;

	task_set_sp(t, frame_sp);
	task_set_ip(t, (uint64_t)(uintptr_t)handler);
	task_set_retval(t, (uint64_t)(uint32_t)sig);
	t->arch.x1 = 0;
	t->arch.x2 = 0;
}
