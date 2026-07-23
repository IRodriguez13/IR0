/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exc_early.c
 * Description: VBAR, SVC ABI, IRQ (timer), EL0 drop, PSCI HVC (F7.2–F7c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "exc_early.h"
#include "pl011.h"
#include "gic_v2.h"
#include "timer.h"
#include "syscall_early.h"
#include "elf_load_early.h"
#include "rr_early.h"

#include <ir0/process.h>
#include <sched/rr_sched.h>

#include <stdint.h>
#include <ir0/boot_log.h>

#define EINVAL 22

/* ESR_EL1: EC 0x15 = SVC instruction from AArch64. */
#define ESR_EC_SHIFT 26
#define ESR_EC_MASK  0x3FUL
#define ESR_EC_SVC64 0x15UL

/* SPSR_EL1: DAIF masked + mode. */
#define SPSR_DAIF_MASKED 0x3C0UL
#define SPSR_MODE_EL0T   0x00UL
#define SPSR_MODE_EL1H   0x05UL

/* PSCI 0.2 (SMC Calling Convention) — QEMU virt uses HVC conduit at EL1. */
#define PSCI_0_2_FN_SYSTEM_OFF   0x84000008UL
#define PSCI_0_2_FN_SYSTEM_RESET 0x84000009UL

#define EL0_STACK_SIZE 4096

extern char ir0_el1_vectors[];
extern void el0_entry(void);

uint8_t el0_stack[EL0_STACK_SIZE] __attribute__((aligned(16)));

static volatile int g_timer_irq_seen;
static int g_el0_svc_tagged;

extern process_t *current_process;

#define RR_TICK_ONESHOT_TICKS 10000U

int arm64_timer_irq_seen(void)
{
	return g_timer_irq_seen;
}

static void psci_hvc(uint64_t fn)
{
	register uint64_t x0 __asm__("x0") = fn;

	__asm__ volatile("hvc #0" : "+r"(x0) :: "memory", "x1", "x2", "x3");
}

void arm64_psci_system_off(void)
{
	ir0_boot_smoke("ARM64_PSCI_OFF");
	psci_hvc(PSCI_0_2_FN_SYSTEM_OFF);
	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

void arm64_psci_system_reset(void)
{
	psci_hvc(PSCI_0_2_FN_SYSTEM_RESET);
	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

int arm64_vbar_early_install(void)
{
	uint64_t vbar = (uint64_t)(uintptr_t)ir0_el1_vectors;

	if ((vbar & 0x7FFUL) != 0)
	{
		return -EINVAL;
	}

	__asm__ volatile("msr vbar_el1, %0" :: "r"(vbar) : "memory");
	__asm__ volatile("isb" ::: "memory");
	return 0;
}

void arm64_exc_trigger_svc(void)
{
	__asm__ volatile("svc #0" ::: "memory");
}

void arm64_exc_sync_el1(void)
{
	uint64_t esr;

	__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
	if (((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) == ESR_EC_SVC64)
	{
		ir0_boot_smoke("ARM64_VBAR_OK");
	}
	else
	{
		ir0_boot_smoke("ARM64_SYNC_OTHER");
	}
}

void arm64_exc_irq_el1(void)
{
	uint32_t iar = arm64_gic_v2_ack();
	uint32_t irq = iar & 0x3ffU;

	arch_timer_oneshot_disarm();

	if (irq == ARM64_GIC_PPI_PHYS_TIMER)
	{
		if (!g_timer_irq_seen)
		{
			g_timer_irq_seen = 1;
			ir0_boot_smoke("ARM64_TIMER_IRQ_OK");
		}

		if (arm64_rr_tick_sched_active())
		{
			process_t *before = current_process;

			rr_schedule_next();
			/* Re-arm only when the IRQ handler resumes (no context switch). */
			if (current_process == before)
				arch_timer_oneshot_arm(RR_TICK_ONESHOT_TICKS);
		}
	}

	if (irq < 1020U)
	{
		arm64_gic_v2_eoi(iar);
	}
}

void arm64_exc_sync_lower(uint64_t *frame)
{
	uint64_t esr;
	int leave = 0;
	int64_t ret;

	__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
	if (((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) != ESR_EC_SVC64)
	{
		ir0_boot_smoke("ARM64_EL0_SYNC_OTHER");
		leave = 1;
	}
	else
	{
		if (!g_el0_svc_tagged)
		{
			g_el0_svc_tagged = 1;
			ir0_boot_smoke("ARM64_EL0_SVC_OK");
		}

		/* frame: x0@0 … x8@8 (pairs of uint64_t). */
		ret = arm64_syscall_early(frame[8], frame[0], frame[1], frame[2],
					  frame[3], frame[4], frame[5], &leave);
		frame[0] = (uint64_t)ret;
	}

	if (leave)
	{
		uint64_t elr;
		uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL1H;

		if (arm64_musl_mode())
			elr = (uint64_t)(uintptr_t)arm64_after_musl;
		else if (arm64_busybox_mode())
			elr = (uint64_t)(uintptr_t)arm64_after_busybox;
		else
			elr = (uint64_t)(uintptr_t)arm64_after_el0;

		__asm__ volatile("msr elr_el1, %0" :: "r"(elr) : "memory");
		__asm__ volatile("msr spsr_el1, %0" :: "r"(spsr) : "memory");
		__asm__ volatile("isb" ::: "memory");
	}
}

void arm64_after_el0(void)
{
	ir0_boot_smoke("ARM64_EL0_RET_OK");
	arm64_psci_system_off();
}

void arm64_enter_el0(void)
{
	uint64_t sp0 = (uint64_t)(uintptr_t)&el0_stack[EL0_STACK_SIZE];
	uint64_t elr = (uint64_t)(uintptr_t)el0_entry;
	uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL0T;

	ir0_boot_smoke("ARM64_EL0_DROP");

	__asm__ volatile(
		"msr	sp_el0, %0\n"
		"msr	elr_el1, %1\n"
		"msr	spsr_el1, %2\n"
		"isb\n"
		"eret\n"
		:
		: "r"(sp0), "r"(elr), "r"(spsr)
		: "memory");
	__builtin_unreachable();
}
