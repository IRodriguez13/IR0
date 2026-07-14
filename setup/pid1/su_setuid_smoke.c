/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: su_setuid_smoke.c
 * Description: IR0 kernel source — su setuid smoke
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include <stdint.h>

#define SYS_geteuid 107
#define SYS_setresuid 117
#define SYS_exit_group 231

#define USER_UID 1000

static long ir0_syscall3(long nr, long a, long b, long c)
{
	long ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a), "S"(b), "d"(c)
		: "rcx", "r11", "memory");

	return ret;
}

static void halt(int code)
{
	ir0_syscall3(SYS_exit_group, code, 0, 0);
	for (;;)
		__asm__ volatile("hlt");
}

int main(void)
{
	long euid = ir0_syscall3(SYS_geteuid, 0, 0, 0);

	if (euid != 0)
		halt(1);

	if (ir0_syscall3(SYS_setresuid, USER_UID, USER_UID, USER_UID) != 0)
		halt(2);

	euid = ir0_syscall3(SYS_geteuid, 0, 0, 0);
	if (euid != USER_UID)
		halt(3);

	if (write(1, "SU_SETUID_OK\n", 13) != 13)
		halt(4);

	halt(0);
	return 0;
}
