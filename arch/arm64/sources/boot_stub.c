/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_stub.c
 * Description: ARM64 QEMU virt early boot — UART tags, early MMU, idle (WFI).
 *
 * PL011 UART0 on QEMU virt is at 0x09000000 (ARM Versatile Express map).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mmu_early.h"
#include "exc_early.h"

#include <stdint.h>

#define UART0_DR   (*(volatile uint32_t *)0x09000000UL)
#define UART0_FR   (*(volatile uint32_t *)0x09000018UL)
#define UART_FR_TXFF (1u << 5)

#define BOOT_STACK_SIZE 4096

/* Global: referenced from _start asm (must be linker-visible). */
uint8_t boot_stack[BOOT_STACK_SIZE] __attribute__((aligned(16)));

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

void boot_main(void)
{
	uart_puts("ARM64_BOOT_OK\n");

	if (arm64_mmu_early_enable() == 0)
	{
		uart_puts("ARM64_MMU_OK\n");
	}
	else
	{
		uart_puts("ARM64_MMU_FAIL\n");
		goto idle;
	}

	if (arm64_vbar_early_install() == 0)
	{
		arm64_exc_trigger_svc();
		uart_puts("ARM64_SVC_RET_OK\n");
		arm64_enter_el0(); /* → EL0 SVC → arm64_after_el0 → PSCI OFF */
	}
	else
	{
		uart_puts("ARM64_VBAR_FAIL\n");
	}

idle:
	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

void __attribute__((section(".text.boot"), noreturn)) _start(void)
{
	/*
	 * QEMU -kernel does not guarantee a usable SP. Set a private stack
	 * before any C calls (needed once mmu_early is a separate TU).
	 */
	__asm__ volatile(
		"adrp	x0, boot_stack\n"
		"add	x0, x0, :lo12:boot_stack\n"
		"add	sp, x0, %[sz]\n"
		"b	boot_main\n"
		:
		: [sz] "i"(BOOT_STACK_SIZE)
		: "x0", "memory");
	__builtin_unreachable();
}
