/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exc_early.c
 * Description: VBAR install, EL1/EL0 SVC handlers, EL0 drop, PSCI HVC (F7.2–F7.3).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "exc_early.h"

#include <stdint.h>

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

#define UART0_DR   (*(volatile uint32_t *)0x09000000UL)
#define UART0_FR   (*(volatile uint32_t *)0x09000018UL)
#define UART_FR_TXFF (1u << 5)

#define EL0_STACK_SIZE 4096

extern char ir0_el1_vectors[];
extern void el0_entry(void);

uint8_t el0_stack[EL0_STACK_SIZE] __attribute__((aligned(16)));

static void uart_putc(char c)
{
	while (UART0_FR & UART_FR_TXFF)
	{
		;
	}
	UART0_DR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
	while (s && *s)
	{
		uart_putc(*s++);
	}
}

static void psci_hvc(uint64_t fn)
{
	register uint64_t x0 __asm__("x0") = fn;

	__asm__ volatile("hvc #0" : "+r"(x0) :: "memory", "x1", "x2", "x3");
}

void arm64_psci_system_off(void)
{
	uart_puts("ARM64_PSCI_OFF\n");
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
		uart_puts("ARM64_VBAR_OK\n");
	}
	else
	{
		uart_puts("ARM64_SYNC_OTHER\n");
	}
}

void arm64_exc_sync_lower(void)
{
	uint64_t esr;
	uint64_t elr = (uint64_t)(uintptr_t)arm64_after_el0;
	uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL1H;

	__asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
	if (((esr >> ESR_EC_SHIFT) & ESR_EC_MASK) == ESR_EC_SVC64)
	{
		uart_puts("ARM64_EL0_SVC_OK\n");
	}
	else
	{
		uart_puts("ARM64_EL0_SYNC_OTHER\n");
	}

	/* Always return to EL1 continuation (avoid EL0 fault storms). */
	__asm__ volatile("msr elr_el1, %0" :: "r"(elr) : "memory");
	__asm__ volatile("msr spsr_el1, %0" :: "r"(spsr) : "memory");
	__asm__ volatile("isb" ::: "memory");
}

void arm64_after_el0(void)
{
	uart_puts("ARM64_EL0_RET_OK\n");
	arm64_psci_system_off();
}

void arm64_enter_el0(void)
{
	uint64_t sp0 = (uint64_t)(uintptr_t)&el0_stack[EL0_STACK_SIZE];
	uint64_t elr = (uint64_t)(uintptr_t)el0_entry;
	uint64_t spsr = SPSR_DAIF_MASKED | SPSR_MODE_EL0T;

	uart_puts("ARM64_EL0_DROP\n");

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
