/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_early.c
 * Description: Freestanding process+TTBR, fork-like clone, exec-like entry replace.
 */

#include "switch_early.h"
#include "mmu_early.h"
#include "pl011.h"
#include "process_early.h"

#include <arch/common/arch_portable.h>
#include <sched/task.h>
#include <stdint.h>
#include <ir0/boot_log.h>

extern void switch_context_arm64(task_t *prev, task_t *next);

#define PROC_STACK_SIZE 4096

static uint8_t g_proc_stack_b[PROC_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t g_fork_stack[PROC_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t g_exec_stack[PROC_STACK_SIZE] __attribute__((aligned(16)));
static struct arm64_cpu_ctx g_proc_ctx_a;
static struct arm64_cpu_ctx g_proc_ctx_b;
static struct arm64_cpu_ctx g_fork_parent;
static struct arm64_cpu_ctx g_fork_child;
static struct arm64_cpu_ctx g_exec_host;
static struct arm64_cpu_ctx g_exec_task;
static uint64_t g_ttbr_a;
static uint64_t g_ttbr_b;
static int g_proc_fail;
static int g_fork_fail;
static int g_exec_ran;

static void process_task_b(void)
{
	uint64_t ttbr;
	volatile uint32_t *probe;

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if ((ttbr & ~0x1UL) != (g_ttbr_b & ~0x1UL))
		g_proc_fail = 1;

	probe = (volatile uint32_t *)(uintptr_t)0x40000000UL;
	(void)*probe;

	ir0_boot_smoke("ARM64_PROCESS_SWITCH_OK");
	arm64_cpu_switch_mm(&g_proc_ctx_b, &g_proc_ctx_a, g_ttbr_a);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

static void fork_child_entry(void)
{
	uint64_t ttbr;
	volatile uint32_t *probe;

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if ((ttbr & ~0x1UL) != (g_ttbr_b & ~0x1UL))
		g_fork_fail = 1;

	probe = (volatile uint32_t *)(uintptr_t)0x40000000UL;
	(void)*probe;

	ir0_boot_smoke("ARM64_FORK_CHILD_OK");
	arm64_cpu_switch_mm(&g_fork_child, &g_fork_parent, g_ttbr_a);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

static void exec_new_entry(void)
{
	g_exec_ran = 1;
	ir0_boot_smoke("ARM64_EXEC_OK");
	arm64_cpu_switch_mm(&g_exec_task, &g_exec_host, g_ttbr_a);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

static void ctx_zero_callee(struct arm64_cpu_ctx *ctx)
{
	ctx->x19 = 0;
	ctx->x20 = 0;
	ctx->x21 = 0;
	ctx->x22 = 0;
	ctx->x23 = 0;
	ctx->x24 = 0;
	ctx->x25 = 0;
	ctx->x26 = 0;
	ctx->x27 = 0;
	ctx->x28 = 0;
	ctx->x29 = 0;
}

int arm64_process_ttbr_smoke(void)
{
	uintptr_t sp_b;
	uint64_t ttbr;

	arm64_mmu_clone_root_b();
	g_ttbr_a = arm64_mmu_root_a();
	g_ttbr_b = arm64_mmu_root_b();
	g_proc_fail = 0;

	sp_b = (uintptr_t)g_proc_stack_b + PROC_STACK_SIZE;
	sp_b &= ~(uintptr_t)0xf;
	ctx_zero_callee(&g_proc_ctx_b);
	g_proc_ctx_b.sp = sp_b;
	g_proc_ctx_b.x30 = (uint64_t)(uintptr_t)process_task_b;

	arm64_cpu_switch_mm(&g_proc_ctx_a, &g_proc_ctx_b, g_ttbr_b);

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if (g_proc_fail || (ttbr & ~0x1UL) != (g_ttbr_a & ~0x1UL))
	{
		ir0_boot_smoke("ARM64_PROCESS_TTBR_FAIL");
		return -1;
	}

	ir0_boot_smoke("ARM64_PROCESS_TTBR_OK");
	return 0;
}

int arm64_fork_exec_smoke(void)
{
	uintptr_t sp;
	uint64_t ttbr;

	arm64_mmu_clone_root_b();
	g_ttbr_a = arm64_mmu_root_a();
	g_ttbr_b = arm64_mmu_root_b();
	g_fork_fail = 0;
	g_exec_ran = 0;

	/* Fork-like: parent keeps TTBR A; child runs on cloned TTBR B. */
	sp = (uintptr_t)g_fork_stack + PROC_STACK_SIZE;
	sp &= ~(uintptr_t)0xf;
	ctx_zero_callee(&g_fork_child);
	g_fork_child.sp = sp;
	g_fork_child.x30 = (uint64_t)(uintptr_t)fork_child_entry;

	arm64_cpu_switch_mm(&g_fork_parent, &g_fork_child, g_ttbr_b);

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if (g_fork_fail || (ttbr & ~0x1UL) != (g_ttbr_a & ~0x1UL))
	{
		ir0_boot_smoke("ARM64_FORK_FAIL");
		return -1;
	}
	ir0_boot_smoke("ARM64_FORK_OK");

	/*
	 * Exec-like: replace entry/sp of a task on the same TTBR (A), switch
	 * to it, then return to host.
	 */
	sp = (uintptr_t)g_exec_stack + PROC_STACK_SIZE;
	sp &= ~(uintptr_t)0xf;
	ctx_zero_callee(&g_exec_task);
	g_exec_task.sp = sp;
	g_exec_task.x30 = (uint64_t)(uintptr_t)exec_new_entry;

	arm64_cpu_switch_mm(&g_exec_host, &g_exec_task, g_ttbr_a);

	if (!g_exec_ran)
	{
		ir0_boot_smoke("ARM64_EXEC_FAIL");
		return -1;
	}

	return 0;
}

static task_t g_pt_task_a;
static task_t g_pt_task_b;
static uint8_t g_pt_stack_b[PROC_STACK_SIZE] __attribute__((aligned(16)));
static int g_pt_fail;

static void process_t_task_b(void)
{
	uint64_t ttbr;

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if ((ttbr & ~0x1UL) != (g_ttbr_b & ~0x1UL))
		g_pt_fail = 1;

	ir0_boot_smoke("ARM64_PROCESS_T_SWITCH_OK");
	switch_context_arm64(&g_pt_task_b, &g_pt_task_a);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

int arm64_process_t_switch_smoke(void)
{
	uintptr_t sp_b;
	uint64_t ttbr;
	unsigned i;
	uint8_t *p;

	arm64_mmu_clone_root_b();
	g_ttbr_a = arm64_mmu_root_a();
	g_ttbr_b = arm64_mmu_root_b();
	g_pt_fail = 0;

	p = (uint8_t *)&g_pt_task_a;
	for (i = 0; i < sizeof(g_pt_task_a); i++)
		p[i] = 0;
	p = (uint8_t *)&g_pt_task_b;
	for (i = 0; i < sizeof(g_pt_task_b); i++)
		p[i] = 0;

	sp_b = (uintptr_t)g_pt_stack_b + PROC_STACK_SIZE;
	sp_b &= ~(uintptr_t)0xf;
	/*
	 * switch_context_arm64 passes &task.arm64.x19 to arm64_cpu_switch;
	 * the SP slot in that layout is arm64.sp_el0.
	 */
	g_pt_task_b.arm64.sp_el0 = sp_b;
	g_pt_task_b.arm64.x30 = (uint64_t)(uintptr_t)process_t_task_b;
	g_pt_task_b.arm64.ttbr0_el1 = g_ttbr_b;
	g_pt_task_a.arm64.ttbr0_el1 = g_ttbr_a;

	switch_context_arm64(&g_pt_task_a, &g_pt_task_b);

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if (g_pt_fail || (ttbr & ~0x1UL) != (g_ttbr_a & ~0x1UL))
	{
		ir0_boot_smoke("ARM64_PROCESS_T_SWITCH_FAIL");
		return -1;
	}

	return 0;
}
