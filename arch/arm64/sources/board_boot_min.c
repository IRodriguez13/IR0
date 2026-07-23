/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: board_boot_min.c
 * Description: Minimal ARM64 board boot — banner + ARCH + ARM64_BOOT_OK only.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "pl011.h"

#include <ir0/arm64_board.h>
#include <ir0/boot_log.h>

#include <stdint.h>

#define BOOT_STACK_SIZE 4096

uint8_t boot_stack[BOOT_STACK_SIZE] __attribute__((aligned(16)));

void boot_main(void)
{
	arm64_board_apply_platform();
	pl011_init();
	ir0_boot_serial_ready();
	arm64_board_log_arch();
	ir0_boot_smoke("ARM64_BOOT_OK");

	for (;;)
	{
		__asm__ volatile("wfi" ::: "memory");
	}
}

void __attribute__((section(".text.boot"), noreturn)) _start(void)
{
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
