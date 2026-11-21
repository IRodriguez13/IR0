/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: signal.c
 * Description: x86-64 IRQ-frame ↔ sigcontext and handler redirect.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/signal_irq.h>
#include <ir0/arch_task.h>
#include <ir0/syscall_frame.h>
#include <ir0/signals.h>
#include <ir0/fpu_backend.h>
#include <asm/debug_trap.h>
#include <config.h>
#include <stddef.h>
#include <string.h>

uint64_t sigcontext_ip(const struct sigcontext *ctx)
{
	return ctx ? ctx->rip : 0;
}

uint64_t sigcontext_sp(const struct sigcontext *ctx)
{
	return ctx ? ctx->rsp : 0;
}

int signal_sigcontext_validate_and_sanitize(struct sigcontext *ctx)
{
    if (!ctx || ctx->rip < 0x00400000ULL ||
        ctx->rip > 0x00007FFFFFFFFFFFULL ||
        ctx->rsp < 0x00400000ULL || ctx->rsp > 0x00007FFFFFFFFFFFULL)
        return 0;

    if (!fpu_sanitize_sigcontext(ctx))
        return 0;
    ctx->rflags = ir0_rflags_sanitize_user(ctx->rflags | 2ULL | RFLAGS_IF);
#if defined(USER_CODE_SEL) && defined(USER_DATA_SEL)
    ctx->cs = (uint64_t)USER_CODE_SEL;
    ctx->ss = (uint64_t)USER_DATA_SEL;
#endif
    return 1;
}

void signal_fill_sigcontext_from_syscall_frame(struct sigcontext *ctx,
						    const struct arch_syscall_frame *sf,
						    uint64_t retval)
{
	if (!ctx || !sf)
		return;

	memset(ctx, 0, sizeof(*ctx));
	ctx->rip = sf->rip;
	ctx->rsp = sf->rsp;
	ctx->rflags = sf->rflags ? sf->rflags : (uint64_t)RFLAGS_IF;
	ctx->rax = retval;
	ctx->rdi = sf->rdi;
	ctx->rsi = sf->rsi;
	ctx->rdx = sf->rdx;
	ctx->r10 = sf->r10;
	ctx->r8 = sf->r8;
	ctx->r9 = sf->r9;
	ctx->rbx = sf->rbx;
	ctx->rbp = sf->rbp;
	ctx->r12 = sf->r12;
	ctx->r13 = sf->r13;
	ctx->r14 = sf->r14;
	ctx->r15 = sf->r15;
#if defined(USER_CODE_SEL) && defined(USER_DATA_SEL)
	ctx->cs = (uint64_t)USER_CODE_SEL;
	ctx->ss = (uint64_t)USER_DATA_SEL;
#endif
	fpu_save_sigcontext(ctx);
}

void signal_fill_syscall_frame_from_sigcontext(struct arch_syscall_frame *sf,
						    const struct sigcontext *ctx)
{
	if (!sf || !ctx)
		return;

	sf->rip = ctx->rip;
	sf->rsp = ctx->rsp;
	sf->rflags = ctx->rflags ? ctx->rflags : (uint64_t)RFLAGS_IF;
	sf->rdi = ctx->rdi;
	sf->rsi = ctx->rsi;
	sf->rdx = ctx->rdx;
	sf->r10 = ctx->r10;
	sf->r8 = ctx->r8;
	sf->r9 = ctx->r9;
	sf->rbx = ctx->rbx;
	sf->rbp = ctx->rbp;
	sf->r12 = ctx->r12;
	sf->r13 = ctx->r13;
	sf->r14 = ctx->r14;
	sf->r15 = ctx->r15;
}

uint64_t signal_sigcontext_return_value(const struct sigcontext *ctx)
{
	return ctx ? ctx->rax : 0;
}

void signal_fill_sigcontext_from_irq_frame(struct sigcontext *ctx,
						const uint64_t *frame)
{
	if (!ctx || !frame)
		return;

	ctx->r15 = frame[-15];
	ctx->r14 = frame[-14];
	ctx->r13 = frame[-13];
	ctx->r12 = frame[-12];
	ctx->r11 = frame[-11];
	ctx->r10 = frame[-10];
	ctx->r9 = frame[-9];
	ctx->r8 = frame[-8];
	ctx->rdi = frame[-7];
	ctx->rsi = frame[-6];
	ctx->rbp = frame[-5];
	ctx->rbx = frame[-4];
	ctx->rdx = frame[-3];
	ctx->rcx = frame[-2];
	ctx->rax = frame[-1];
	ctx->orig_rax = 0;
	ctx->rip = frame[2];
	ctx->cs = frame[3];
	ctx->rflags = frame[4];
	ctx->rsp = frame[5];
	ctx->ss = frame[6];
	fpu_save_sigcontext(ctx);
}

uint64_t irq_frame_sp(const uint64_t *frame)
{
	return frame ? frame[5] : 0;
}

void signal_redirect_irq_frame(uint64_t *frame, void *handler, int sig,
				    uint64_t new_sp, uint64_t info_addr,
				    uint64_t uctx_addr, int sa_siginfo,
				    uint64_t restorer)
{
	/* The handler's `ret` pops @restorer from SIGFRAME_RETURN_SLOT. */
	(void)restorer;
	if (!frame || !handler)
		return;

	fpu_signal_enter();
	frame[2] = (uint64_t)(uintptr_t)handler;
	frame[5] = new_sp;
	frame[-7] = (uint64_t)(uint32_t)sig;
	if (sa_siginfo)
	{
		frame[-6] = info_addr;
		frame[-3] = uctx_addr;
	}
	else
	{
		frame[-6] = 0;
		frame[-3] = 0;
	}
}

void signal_set_return(struct arch_syscall_frame *sf, uint64_t restorer)
{
	/* The handler's `ret` pops the restorer from SIGFRAME_RETURN_SLOT. */
	(void)sf;
	(void)restorer;
	fpu_signal_enter();
}

/* Linux x86-64 ucontext_t. gregs follow the sigcontext register order. */
struct ir0_x86_ucontext
{
	unsigned long uc_flags;
	unsigned long uc_link;
	unsigned long ss_sp;
	int ss_flags;
	int ss_pad;
	unsigned long ss_size;
	unsigned long gregs[23];
	unsigned long fpregs;
	unsigned long reserved1[8];
	unsigned long sigmask[16];
	unsigned char fpregs_mem[512];
};

_Static_assert(sizeof(struct ir0_x86_ucontext) == IR0_UCONTEXT_BYTES,
	       "x86-64 ucontext_t size");
_Static_assert(offsetof(struct ir0_x86_ucontext, gregs) + 16 * sizeof(unsigned long) == 168,
	       "x86-64 ucontext RIP offset");

void ucontext_export(const struct sigcontext *ctx, uint64_t user_va,
			  uint64_t mask, void *image)
{
	struct ir0_x86_ucontext *uc = image;

	if (!ctx || !image)
		return;
	memset(uc, 0, sizeof(*uc));
	uc->gregs[0] = ctx->r8;
	uc->gregs[1] = ctx->r9;
	uc->gregs[2] = ctx->r10;
	uc->gregs[3] = ctx->r11;
	uc->gregs[4] = ctx->r12;
	uc->gregs[5] = ctx->r13;
	uc->gregs[6] = ctx->r14;
	uc->gregs[7] = ctx->r15;
	uc->gregs[8] = ctx->rdi;
	uc->gregs[9] = ctx->rsi;
	uc->gregs[10] = ctx->rbp;
	uc->gregs[11] = ctx->rbx;
	uc->gregs[12] = ctx->rdx;
	uc->gregs[13] = ctx->rax;
	uc->gregs[14] = ctx->rcx;
	uc->gregs[15] = ctx->rsp;
	uc->gregs[16] = ctx->rip;
	uc->gregs[17] = ctx->rflags;
	uc->gregs[18] = (ctx->cs & 0xffffULL) | ((ctx->ss & 0xffffULL) << 48);
	uc->fpregs = user_va + offsetof(struct ir0_x86_ucontext, fpregs_mem);
	memcpy(uc->fpregs_mem, &ctx->fpstate, sizeof(uc->fpregs_mem));
	uc->sigmask[0] = mask;
}

void ucontext_import(const void *image, uint64_t user_va,
			  struct sigcontext *ctx)
{
	const struct ir0_x86_ucontext *uc = image;

	if (!image || !ctx)
		return;
	ctx->r8 = uc->gregs[0];
	ctx->r9 = uc->gregs[1];
	ctx->r10 = uc->gregs[2];
	ctx->r11 = uc->gregs[3];
	ctx->r12 = uc->gregs[4];
	ctx->r13 = uc->gregs[5];
	ctx->r14 = uc->gregs[6];
	ctx->r15 = uc->gregs[7];
	ctx->rdi = uc->gregs[8];
	ctx->rsi = uc->gregs[9];
	ctx->rbp = uc->gregs[10];
	ctx->rbx = uc->gregs[11];
	ctx->rdx = uc->gregs[12];
	ctx->rax = uc->gregs[13];
	ctx->rcx = uc->gregs[14];
	ctx->rsp = uc->gregs[15];
	ctx->rip = uc->gregs[16];
	ctx->rflags = uc->gregs[17];
	if (uc->fpregs == user_va + offsetof(struct ir0_x86_ucontext, fpregs_mem))
		memcpy(&ctx->fpstate, uc->fpregs_mem, sizeof(uc->fpregs_mem));
	(void)user_va;
}
