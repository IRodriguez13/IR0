/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal.c
 * Description: ARM64 exception-frame ↔ Linux aarch64 sigcontext delivery.
 *
 * Frame layout matches vectors.S exc_entry_frame (x0@0 … x30@240).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/signal_irq.h>
#include <ir0/arch_task.h>
#include <ir0/syscall_frame.h>
#include <ir0/signals.h>
#include <stdint.h>
#include <asm/exc_frame.h>
#include <string.h>

uint64_t sigcontext_ip(const struct sigcontext *ctx)
{
	return ctx ? ctx->pc : 0;
}

uint64_t sigcontext_sp(const struct sigcontext *ctx)
{
	return ctx ? ctx->sp : 0;
}

uint64_t sigcontext_arg(const struct sigcontext *ctx, unsigned index)
{
	return (ctx && index < 6) ? ctx->regs[index] : 0;
}

void sigcontext_set_arg(struct sigcontext *ctx, unsigned index, uint64_t value)
{
	if (ctx && index < 6)
		ctx->regs[index] = value;
}

int signal_sigcontext_validate_and_sanitize(struct sigcontext *ctx)
{
    if (!ctx || ctx->pc < 0x00400000ULL ||
        ctx->pc > 0x0000FFFFFFFFFFFFULL ||
        ctx->sp < 0x00400000ULL || ctx->sp > 0x0000FFFFFFFFFFFFULL ||
        (ctx->pc & 3ULL) != 0 || (ctx->sp & 15ULL) != 0)
        return 0;

    /* Preserve userspace NZCV only; force EL0t and drop privileged PSTATE. */
    ctx->pstate &= 0xF0000000ULL;
    return 1;
}

void signal_fill_sigcontext_from_syscall_frame(struct sigcontext *ctx,
						    const struct arch_syscall_frame *sf,
						    uint64_t retval)
{
	if (!ctx || !sf)
		return;

	memset(ctx, 0, sizeof(*ctx));
	ctx->regs[0] = retval;
	ctx->regs[1] = sf->x1;
	ctx->regs[2] = sf->x2;
	ctx->regs[3] = sf->x3;
	ctx->regs[4] = sf->x4;
	ctx->regs[5] = sf->x5;
	ctx->regs[6] = sf->x6;
	ctx->regs[7] = sf->x7;
	ctx->regs[8] = sf->x8;
	ctx->regs[9] = sf->x9;
	ctx->regs[10] = sf->x10;
	ctx->regs[11] = sf->x11;
	ctx->regs[12] = sf->x12;
	ctx->regs[13] = sf->x13;
	ctx->regs[14] = sf->x14;
	ctx->regs[15] = sf->x15;
	ctx->regs[16] = sf->x16;
	ctx->regs[17] = sf->x17;
	ctx->regs[18] = sf->x18;
	ctx->regs[19] = sf->x19;
	ctx->regs[20] = sf->x20;
	ctx->regs[21] = sf->x21;
	ctx->regs[22] = sf->x22;
	ctx->regs[23] = sf->x23;
	ctx->regs[24] = sf->x24;
	ctx->regs[25] = sf->x25;
	ctx->regs[26] = sf->x26;
	ctx->regs[27] = sf->x27;
	ctx->regs[28] = sf->x28;
	ctx->regs[29] = sf->x29;
	ctx->regs[30] = sf->x30;
	ctx->sp = sf->sp;
	ctx->pc = sf->elr;
	ctx->pstate = sf->spsr;
}

void signal_fill_syscall_frame_from_sigcontext(struct arch_syscall_frame *sf,
						    const struct sigcontext *ctx)
{
	uint64_t *regs;
	unsigned i;

	if (!sf || !ctx)
		return;

	/* x0..x30 are the leading, contiguous fields of the ARM64 frame. */
	regs = &sf->x0;
	for (i = 0; i < 31; i++)
		regs[i] = ctx->regs[i];
	sf->sp = ctx->sp;
	sf->elr = ctx->pc;
	sf->spsr = ctx->pstate;
}

uint64_t signal_sigcontext_return_value(const struct sigcontext *ctx)
{
	return ctx ? ctx->regs[0] : 0;
}

void signal_fill_sigcontext_from_irq_frame(struct sigcontext *ctx,
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

	/* Return state comes from the saved frame (asm/exc_frame.h). */
	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	elr = frame[ARM64_EXC_IDX_ELR];
	spsr = frame[ARM64_EXC_IDX_SPSR];
	sp_el0 = frame[ARM64_EXC_IDX_SP];
	ctx->fault_address = far;
	ctx->pc = elr;
	ctx->pstate = spsr;
	ctx->sp = sp_el0;
}

uint64_t irq_frame_sp(const uint64_t *frame)
{
	return frame ? frame[ARM64_EXC_IDX_SP] : 0;
}

void signal_redirect_irq_frame(uint64_t *frame, void *handler, int sig,
				    uint64_t new_rsp, uint64_t info_addr,
				    uint64_t uctx_addr, int sa_siginfo,
				    uint64_t restorer)
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

	/* Linux arm64 setup_return(): the handler returns into x30. */
	frame[30] = restorer;

	/* vectors.S restores ELR/SP_EL0 from the frame on eret. */
	frame[ARM64_EXC_IDX_ELR] = elr;
	frame[ARM64_EXC_IDX_SP] = new_rsp;
}

void signal_set_return(struct arch_syscall_frame *sf, uint64_t restorer)
{
	/* Linux arm64 setup_return(): regs[30] = sigtramp. */
	if (sf)
		sf->x30 = restorer;
}
