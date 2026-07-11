/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_stub.c
 * Description: ARM64 QEMU virt early boot — UART0 print + idle (WFI).
 *
 * PL011 UART0 on QEMU virt is at 0x09000000 (ARM Versatile Express map).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

#define UART0_DR   (*(volatile uint32_t *)0x09000000UL)
#define UART0_FR   (*(volatile uint32_t *)0x09000018UL)
#define UART_FR_TXFF (1u << 5)

static void uart_putc(char c)
{
	while (UART0_FR & UART_FR_TXFF)
		;
	UART0_DR = (uint32_t)(uint8_t)c;
}

static void uart_puts(const char *s)
{
	while (s && *s)
		uart_putc(*s++);
}

void _start(void)
{
	uart_puts("ARM64_BOOT_OK\n");
	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}
