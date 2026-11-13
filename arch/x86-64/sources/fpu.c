/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fpu.c
 * Description: x86-64 per-task FP/SIMD state with FXSAVE/FXRSTOR. The
 *              kernel is built without SSE, so the live registers always
 *              belong to the current task: __switch_to saves them eagerly
 *              and loads the next task's (Linux eager FPU switching).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/fpu_backend.h>
#include <string.h>

/* FNINIT control word and the power-on MXCSR (all exceptions masked). */
#define FPU_INIT_CWD 0x037fU
#define FPU_INIT_MXCSR 0x1f80U
/* MXCSR_MASK when FXSAVE reports 0 (SDM: every bit but DAZ). */
#define FPU_DEFAULT_MXCSR_MASK 0xffbfU

static const struct _fpstate_64 fpu_init_state = {
	.cwd = FPU_INIT_CWD,
	.mxcsr = FPU_INIT_MXCSR,
};

static uint32_t fpu_mxcsr_mask = FPU_DEFAULT_MXCSR_MASK;

static void fxsave(struct _fpstate_64 *state)
{
	__asm__ volatile("fxsave64 %0" : "=m"(*state));
}

static void fxrstor(const struct _fpstate_64 *state)
{
	__asm__ volatile("fxrstor64 %0" : : "m"(*state));
}

static int task_is_current(const task_t *task)
{
	return current_process && task == &current_process->task;
}

static const struct _fpstate_64 *task_fpu(const task_t *task)
{
	return task->arch.fpu_valid ? &task->arch.fpu : &fpu_init_state;
}

void fpu_init(void)
{
	struct _fpstate_64 probe;

	memset(&probe, 0, sizeof(probe));
	__asm__ volatile("fninit");
	fxsave(&probe);
	if (probe.mxcsr_mask)
		fpu_mxcsr_mask = probe.mxcsr_mask;
	fxrstor(&fpu_init_state);
}

void fpu_switch(task_t *prev, task_t *next)
{
	if (!next || prev == next)
		return;
	if (prev)
	{
		fxsave(&prev->arch.fpu);
		prev->arch.fpu_valid = 1;
	}
	fxrstor(task_fpu(next));
}

void fpu_copy_current(task_t *child)
{
	if (!child)
		return;
	fxsave(&child->arch.fpu);
	child->arch.fpu_valid = 1;
}

void fpu_flush_thread(task_t *task)
{
	if (!task)
		return;
	task->arch.fpu_valid = 0;
	if (task_is_current(task))
		fxrstor(&fpu_init_state);
}

void fpu_save_sigcontext(struct sigcontext *ctx)
{
	if (ctx)
		fxsave(&ctx->fpstate);
}

void fpu_signal_enter(void)
{
	fxrstor(&fpu_init_state);
}

int fpu_sanitize_sigcontext(struct sigcontext *ctx)
{
	if (!ctx)
		return 0;
	ctx->fpstate.mxcsr &= fpu_mxcsr_mask;
	return 1;
}

void fpu_load_sigcontext(task_t *task, const struct sigcontext *ctx)
{
	if (!task || !ctx)
		return;
	task->arch.fpu = ctx->fpstate;
	task->arch.fpu_valid = 1;
	if (task_is_current(task))
		fxrstor(&task->arch.fpu);
}
