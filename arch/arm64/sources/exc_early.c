/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exc_early.c
 * Description: Install VBAR_EL1 and handle EL1 sync (SVC) for F7.2 smoke.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "exc_early.h"

#include <stdint.h>

#define EINVAL 22

/* ESR_EL1: EC 0x15 = SVC instruction from AArch64. */
#define ESR_EC_SHIFT 26
#define ESR_EC_MASK  0x3FUL
#define ESR_EC_SVC64 0x15UL

#define UART0_DR   (*(volatile uint32_t *)0x09000000UL)
#define UART0_FR   (*(volatile uint32_t *)0x09000018UL)
#define UART_FR_TXFF (1u << 5)

extern char ir0_el1_vectors[];

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
