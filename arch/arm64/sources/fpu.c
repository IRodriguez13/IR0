/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fpu.c
 * Description: ARM64 per-task FP/SIMD state (v0..v31, FPSR, FPCR). The
 *              kernel is built with -mgeneral-regs-only, so the live
 *              registers always belong to the current task: __switch_to
 *              saves them eagerly and loads the next task's.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/fpu_backend.h>
#include <string.h>

/* CPACR_EL1.FPEN = 0b11: FP/SIMD does not trap at EL0 or EL1. */
#define CPACR_EL1_FPEN (3UL << 20)

static const struct user_fpsimd_state fpsimd_init_state;

static void fpsimd_save(struct user_fpsimd_state *state)
{
	uint64_t fpsr;
	uint64_t fpcr;

	__asm__ volatile(
		".arch_extension fp\n\t"
		".arch_extension simd\n\t"
		"stp q0, q1, [%2, #0]\n\t"
		"stp q2, q3, [%2, #32]\n\t"
		"stp q4, q5, [%2, #64]\n\t"
		"stp q6, q7, [%2, #96]\n\t"
		"stp q8, q9, [%2, #128]\n\t"
		"stp q10, q11, [%2, #160]\n\t"
		"stp q12, q13, [%2, #192]\n\t"
		"stp q14, q15, [%2, #224]\n\t"
		"stp q16, q17, [%2, #256]\n\t"
		"stp q18, q19, [%2, #288]\n\t"
		"stp q20, q21, [%2, #320]\n\t"
		"stp q22, q23, [%2, #352]\n\t"
		"stp q24, q25, [%2, #384]\n\t"
		"stp q26, q27, [%2, #416]\n\t"
		"stp q28, q29, [%2, #448]\n\t"
		"stp q30, q31, [%2, #480]\n\t"
		"mrs %0, fpsr\n\t"
		"mrs %1, fpcr\n\t"
		: "=&r"(fpsr), "=&r"(fpcr)
		: "r"(state->vregs)
		: "memory");
	state->fpsr = (uint32_t)fpsr;
	state->fpcr = (uint32_t)fpcr;
}

static void fpsimd_load(const struct user_fpsimd_state *state)
{
	__asm__ volatile(
		".arch_extension fp\n\t"
		".arch_extension simd\n\t"
		"ldp q0, q1, [%0, #0]\n\t"
		"ldp q2, q3, [%0, #32]\n\t"
		"ldp q4, q5, [%0, #64]\n\t"
		"ldp q6, q7, [%0, #96]\n\t"
		"ldp q8, q9, [%0, #128]\n\t"
		"ldp q10, q11, [%0, #160]\n\t"
		"ldp q12, q13, [%0, #192]\n\t"
		"ldp q14, q15, [%0, #224]\n\t"
		"ldp q16, q17, [%0, #256]\n\t"
		"ldp q18, q19, [%0, #288]\n\t"
		"ldp q20, q21, [%0, #320]\n\t"
		"ldp q22, q23, [%0, #352]\n\t"
		"ldp q24, q25, [%0, #384]\n\t"
		"ldp q26, q27, [%0, #416]\n\t"
		"ldp q28, q29, [%0, #448]\n\t"
		"ldp q30, q31, [%0, #480]\n\t"
		"msr fpsr, %1\n\t"
		"msr fpcr, %2\n\t"
		:
		: "r"(state->vregs), "r"((uint64_t)state->fpsr),
		  "r"((uint64_t)state->fpcr)
		: "memory");
}

static int task_is_current(const task_t *task)
{
	return current_process && task == &current_process->task;
}

void fpu_init(void)
{
	uint64_t cpacr;

	__asm__ volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
	cpacr |= CPACR_EL1_FPEN;
	__asm__ volatile("msr cpacr_el1, %0\n\tisb" : : "r"(cpacr) : "memory");
	fpsimd_load(&fpsimd_init_state);
}

void fpu_switch(task_t *prev, task_t *next)
{
	if (!next || prev == next)
		return;
	if (prev)
		fpsimd_save(&prev->arch.fpsimd);
	fpsimd_load(&next->arch.fpsimd);
}

void fpu_copy_current(task_t *child)
{
	if (child)
		fpsimd_save(&child->arch.fpsimd);
}

void fpu_flush_thread(task_t *task)
{
	if (!task)
		return;
	task->arch.fpsimd = fpsimd_init_state;
	if (task_is_current(task))
		fpsimd_load(&fpsimd_init_state);
}

/* Linux preserve_fpsimd_context(): the first record of __reserved. */
void fpu_save_sigcontext(struct sigcontext *ctx)
{
	struct fpsimd_context *rec;
	struct user_fpsimd_state live;

	if (!ctx)
		return;
	fpsimd_save(&live);
	rec = (struct fpsimd_context *)ctx->__reserved;
	rec->head.magic = FPSIMD_MAGIC;
	rec->head.size = sizeof(*rec);
	rec->fpsr = live.fpsr;
	rec->fpcr = live.fpcr;
	memcpy(rec->vregs, live.vregs, sizeof(rec->vregs));
	/* The next record header stays zero: the list terminator. */
	memset(ctx->__reserved + sizeof(*rec), 0, sizeof(struct _aarch64_ctx));
}

void fpu_signal_enter(void)
{
}

int fpu_sanitize_sigcontext(struct sigcontext *ctx)
{
	const struct fpsimd_context *rec;

	if (!ctx)
		return 0;
	rec = (const struct fpsimd_context *)ctx->__reserved;
	return rec->head.magic == FPSIMD_MAGIC &&
	       rec->head.size == sizeof(*rec);
}

/* Linux restore_fpsimd_context(). */
void fpu_load_sigcontext(task_t *task, const struct sigcontext *ctx)
{
	const struct fpsimd_context *rec;

	if (!task || !ctx)
		return;
	rec = (const struct fpsimd_context *)ctx->__reserved;
	memcpy(task->arch.fpsimd.vregs, rec->vregs,
	       sizeof(task->arch.fpsimd.vregs));
	task->arch.fpsimd.fpsr = rec->fpsr;
	task->arch.fpsimd.fpcr = rec->fpcr;
	if (task_is_current(task))
		fpsimd_load(&task->arch.fpsimd);
}
