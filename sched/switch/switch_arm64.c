/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch_arm64.c
 * Description: ARM64 EL1 cooperative context switch with optional TTBR0.
 */

#include <ir0/task.h>
#include <stdint.h>

extern void arm64_cpu_switch(void *prev_ctx, void *next_ctx);
extern void arm64_cpu_switch_mm(void *prev_ctx, void *next_ctx, uint64_t next_ttbr);

void switch_context_arm64(task_t *prev, task_t *next)
{
	static uint64_t discard_ctx[13];
	void *prev_ctx;
	void *next_ctx;
	uint64_t next_ttbr;

	if (!next)
		return;

	/*
	 * Callee-saved block starts at arch.x19; arm64_cpu_ctx.sp overlays
	 * arch.sp_el0 (see process_early TTBR smoke).
	 */
	prev_ctx = prev ? (void *)&prev->arch.x19 : (void *)discard_ctx;
	next_ctx = (void *)&next->arch.x19;
	next_ttbr = next->arch.ttbr0_el1;

	if (next_ttbr)
		arm64_cpu_switch_mm(prev_ctx, next_ctx, next_ttbr);
	else
		arm64_cpu_switch(prev_ctx, next_ctx);
}
