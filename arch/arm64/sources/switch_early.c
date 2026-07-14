/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch_early.c
 * Description: F7h freestanding two-stack EL1 switch smoke.
 */

#include "switch_early.h"
#include "pl011.h"

#include <arch/common/arch_portable.h>
#include <stdint.h>

#define SWITCH_STACK_SIZE 4096

static uint8_t g_stack_b[SWITCH_STACK_SIZE] __attribute__((aligned(16)));
static struct arm64_cpu_ctx g_ctx_a;
static struct arm64_cpu_ctx g_ctx_b;

void arm64_cpu_switch_mm(struct arm64_cpu_ctx *prev, struct arm64_cpu_ctx *next,
			 uint64_t next_ttbr)
{
	if (next_ttbr)
		arch_mm_activate((uintptr_t)next_ttbr);
	arm64_cpu_switch(prev, next);
}

static void switch_task_b(void)
{
	pl011_puts("ARM64_SWITCH_B\n");
	arm64_cpu_switch(&g_ctx_b, &g_ctx_a);
	/* Should not return: A prints SWITCH_OK and continues boot. */
	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

void arm64_switch_early_smoke(void)
{
	uintptr_t sp_b;

	/* Fresh B context: LR → switch_task_b; SP → stack top. */
	sp_b = (uintptr_t)g_stack_b + SWITCH_STACK_SIZE;
	sp_b &= ~(uintptr_t)0xf;
	g_ctx_b.x19 = 0;
	g_ctx_b.x20 = 0;
	g_ctx_b.x21 = 0;
	g_ctx_b.x22 = 0;
	g_ctx_b.x23 = 0;
	g_ctx_b.x24 = 0;
	g_ctx_b.x25 = 0;
	g_ctx_b.x26 = 0;
	g_ctx_b.x27 = 0;
	g_ctx_b.x28 = 0;
	g_ctx_b.x29 = 0;
	g_ctx_b.sp = sp_b;
	g_ctx_b.x30 = (uint64_t)(uintptr_t)switch_task_b;

	arm64_cpu_switch(&g_ctx_a, &g_ctx_b);
	pl011_puts("ARM64_SWITCH_OK\n");
}
