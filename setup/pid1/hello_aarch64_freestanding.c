/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: hello_aarch64_freestanding.c
 * Description: CRT-free aarch64 hello for early ARM64 embed smokes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>

/* Linux aarch64: write=64, exit=93 */
void _start(void)
{
	static const char msg[] = "IR0_MUSL_AARCH64_HELLO_OK\n";
	register long x0 __asm__("x0");
	register long x1 __asm__("x1");
	register long x2 __asm__("x2");
	register long x8 __asm__("x8");

	x0 = 1;
	x1 = (long)msg;
	x2 = (long)(sizeof(msg) - 1);
	x8 = 64;
	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x1), "r"(x2), "r"(x8)
			 : "memory", "cc");

	x0 = 0;
	x8 = 93;
	__asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");

	for (;;)
		;
}
