/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_task_context_arm64.h
 * Description: ARM64 primary CPU register snapshot for per-task context switching.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Linux struct user_fpsimd_state: v0..v31, FPSR and FPCR. All zero is the
 * initial state (round to nearest, no exception traps).
 */
struct user_fpsimd_state
{
	__uint128_t vregs[32];
	uint32_t fpsr;
	uint32_t fpcr;
	uint32_t __reserved[2];
};

typedef struct arch_task_context
{
	uint64_t x0;
	uint64_t x1;
	uint64_t x2;
	uint64_t x3;
	uint64_t x4;
	uint64_t x5;
	uint64_t x6;
	uint64_t x7;
	uint64_t x8;
	uint64_t x9;
	uint64_t x10;
	uint64_t x11;
	uint64_t x12;
	uint64_t x13;
	uint64_t x14;
	uint64_t x15;
	uint64_t x16;
	uint64_t x17;
	uint64_t x18;
	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29;
	uint64_t x30;
	uint64_t sp_el0;
	uint64_t elr_el1;
	uint64_t spsr_el1;
	uint64_t ttbr0_el1;
	/*
	 * Kernel continuation saved by arm64_cpu_switch (Linux
	 * thread.cpu_context): x19..x30 then SP. Kept apart from the user
	 * return state above, which it used to overlay.
	 */
	uint64_t kctx[13];
	/* Non-zero once kctx holds a real continuation of this task. */
	uint64_t kctx_valid;
	/* User FP/SIMD registers while the task is switched out (Linux
	 * thread.uw.fpsimd_state). */
	struct user_fpsimd_state fpsimd;
} arch_task_context_t;

#define ARM64_KCTX_LR 11
#define ARM64_KCTX_SP 12

#define TASK_CONTEXT_ASSERT_LAYOUT(task_type) \
	_Static_assert(sizeof(task_type) >= sizeof(arch_task_context_t), \
		       "ARM64 task context must remain embedded")

#define PROCESS_CONTEXT_ASSERT_LAYOUT(process_type) \
	_Static_assert(offsetof(process_type, task) == 0, \
		       "process task must remain first")
