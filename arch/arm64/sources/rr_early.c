/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: rr_early.c
 * Description: Freestanding RR queue smoke via rr_add_process + switch_arm64.
 *
 * Bring-up exception (not production): may call rr_add_process and
 * switch_context_arm64 directly. Production backends must use
 * <ir0/sched.h> / sched_context_switch_to → switch_to() only.
 */

#include "rr_early.h"
#include "gic_v2.h"
#include "mmu_early.h"
#include "pl011.h"
#include "timer.h"

#include <arch/common/arch_portable.h>
#include <ir0/process.h>
#include <sched/rr_sched.h>
#include <sched/task.h>
#include <stdint.h>
#include <ir0/boot_log.h>

extern void switch_context_arm64(task_t *prev, task_t *next);

#define RR_STACK_SIZE 4096
#define RR_TICK_ONESHOT_TICKS 10000U
#define RR_TICK_POLL_SPINS 2000000U

static process_t g_rr_pa;
static process_t g_rr_pb;
static task_t g_rr_smoke_ctx;
static uint8_t g_rr_stack_a[RR_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t g_rr_stack_b[RR_STACK_SIZE] __attribute__((aligned(16)));
static uint64_t g_rr_ttbr_a;
static uint64_t g_rr_ttbr_b;
static int g_rr_fail;
static int g_rr_ran;
static volatile int g_rr_tick_active;
static volatile int g_rr_tick_seen;

process_t *current_process;

int arm64_rr_tick_sched_active(void)
{
	return g_rr_tick_active != 0;
}

static void zero_proc(process_t *p)
{
	uint8_t *b = (uint8_t *)p;
	unsigned i;

	for (i = 0; i < sizeof(*p); i++)
		b[i] = 0;
}

static void zero_task(task_t *t)
{
	uint8_t *b = (uint8_t *)t;
	unsigned i;

	for (i = 0; i < sizeof(*t); i++)
		b[i] = 0;
}

static void rr_task_b(void)
{
	uint64_t ttbr;

	__asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
	if ((ttbr & ~0x1UL) != (g_rr_ttbr_b & ~0x1UL))
		g_rr_fail = 1;
	g_rr_ran = 1;
	ir0_boot_smoke("ARM64_RR_SCHED_OK");
	switch_context_arm64(&g_rr_pb.task, &g_rr_pa.task);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

static void rr_tick_task_b(void)
{
	/*
	 * Reached only after timer IRQ calls rr_schedule_next() from task A.
	 * IRQ handler does not resume after a real context switch.
	 */
	if (!g_rr_tick_seen)
	{
		g_rr_tick_seen = 1;
		ir0_boot_smoke("ARM64_RR_TICK_OK");
	}

	g_rr_tick_active = 0;
	arch_timer_oneshot_disarm();
	switch_context_arm64(&g_rr_pb.task, &g_rr_smoke_ctx);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

static void rr_tick_task_a(void)
{
	unsigned spins;
	unsigned long irqf;

	g_rr_tick_active = 1;
	if (arm64_gic_v2_enable(ARM64_GIC_PPI_PHYS_TIMER) != 0)
	{
		g_rr_fail = 1;
		g_rr_tick_active = 0;
		switch_context_arm64(&g_rr_pa.task, &g_rr_smoke_ctx);
		for (;;)
			__asm__ volatile("wfi" ::: "memory");
	}

	irqf = irq_save();
	arch_timer_oneshot_arm(RR_TICK_ONESHOT_TICKS);
	irq_restore(irqf & ~(1UL << 7));

	for (spins = 0; spins < RR_TICK_POLL_SPINS && !g_rr_tick_seen; spins++)
		__asm__ volatile("wfi" ::: "memory");

	g_rr_tick_active = 0;
	arch_timer_oneshot_disarm();
	switch_context_arm64(&g_rr_pa.task, &g_rr_smoke_ctx);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

static void rr_setup_proc_stack(process_t *p, uint8_t *stack, void (*entry)(void))
{
	uintptr_t sp;

	sp = (uintptr_t)stack + RR_STACK_SIZE;
	sp &= ~(uintptr_t)0xf;
	p->task.arm64.sp_el0 = sp;
	p->task.arm64.x30 = (uint64_t)(uintptr_t)entry;
}

static int arm64_rr_tick_smoke(void)
{
	g_rr_tick_seen = 0;
	g_rr_tick_active = 0;

	zero_task(&g_rr_pa.task);
	zero_task(&g_rr_pb.task);
	g_rr_pa.task.arm64.ttbr0_el1 = g_rr_ttbr_a;
	g_rr_pb.task.arm64.ttbr0_el1 = g_rr_ttbr_b;
	rr_setup_proc_stack(&g_rr_pa, g_rr_stack_a, rr_tick_task_a);
	rr_setup_proc_stack(&g_rr_pb, g_rr_stack_b, rr_tick_task_b);

	g_rr_pa.state = PROCESS_READY;
	g_rr_pb.state = PROCESS_READY;

	rr_promote_process(&g_rr_pa);
	current_process = &g_rr_pa;
	g_rr_pa.state = PROCESS_RUNNING;

	switch_context_arm64(&g_rr_smoke_ctx, &g_rr_pa.task);

	if (!g_rr_tick_seen)
	{
		ir0_boot_smoke("ARM64_RR_TICK_FAIL");
		return -1;
	}
	return 0;
}

int arm64_rr_sched_smoke(void)
{
	uintptr_t sp_b;
	int n;

	arm64_mmu_clone_root_b();
	g_rr_ttbr_a = arm64_mmu_root_a();
	g_rr_ttbr_b = arm64_mmu_root_b();
	g_rr_fail = 0;
	g_rr_ran = 0;
	g_rr_tick_seen = 0;
	g_rr_tick_active = 0;
	current_process = NULL;

	zero_proc(&g_rr_pa);
	zero_proc(&g_rr_pb);
	zero_task(&g_rr_smoke_ctx);

	g_rr_pa.state = PROCESS_READY;
	g_rr_pb.state = PROCESS_READY;
	g_rr_pa.task.arm64.ttbr0_el1 = g_rr_ttbr_a;
	g_rr_pb.task.arm64.ttbr0_el1 = g_rr_ttbr_b;

	sp_b = (uintptr_t)g_rr_stack_b + RR_STACK_SIZE;
	sp_b &= ~(uintptr_t)0xf;
	g_rr_pb.task.arm64.sp_el0 = sp_b;
	g_rr_pb.task.arm64.x30 = (uint64_t)(uintptr_t)rr_task_b;

	rr_add_process(&g_rr_pa);
	rr_add_process(&g_rr_pb);
	n = rr_count_runnable();
	if (n < 2)
	{
		ir0_boot_smoke("ARM64_RR_SCHED_FAIL");
		return -1;
	}

	current_process = &g_rr_pa;
	g_rr_pa.state = PROCESS_RUNNING;
	switch_context_arm64(&g_rr_pa.task, &g_rr_pb.task);

	if (g_rr_fail || !g_rr_ran)
	{
		ir0_boot_smoke("ARM64_RR_SCHED_FAIL");
		return -1;
	}

	if (arm64_rr_tick_smoke() != 0)
		return -1;

	return 0;
}
