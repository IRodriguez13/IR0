/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: musl_pthread_smoke.c
 * Description: IR0 kernel source — musl pthread smoke
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include <stdint.h>

#define SYS_clone      56
#define SYS_exit       60
#define SYS_exit_group 231
#define SYS_nanosleep  35

struct smoke_timespec
{
	long tv_sec;
	long tv_nsec;
};

#define CLONE_VM              0x00000100
#define CLONE_THREAD          0x00010000
#define CLONE_CHILD_SETTID    0x01000000

static volatile int worker_ran;
static char child_stack[16384] __attribute__((aligned(16)));

static long ir0_syscall6(long nr, long a, long b, long c, long d, long e, long f)
{
	long ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a), "S"(b), "d"(c), "r"(d), "r"(e), "r"(f)
		: "rcx", "r11", "memory");

	return ret;
}

static int child_entry(void *arg)
{
	(void)arg;
	worker_ran = 1;
	ir0_syscall6(SYS_exit, 0, 0, 0, 0, 0, 0);
	return 0;
}

static void halt_exit(int code)
{
	ir0_syscall6(SYS_exit_group, code, 0, 0, 0, 0, 0);
	for (;;)
		__asm__ volatile("hlt");
}

int main(void)
{
	int tid;
	uintptr_t sp = (uintptr_t)(child_stack + sizeof(child_stack));

	tid = (int)ir0_syscall6(SYS_clone,
				CLONE_VM | CLONE_THREAD | CLONE_CHILD_SETTID,
				(long)sp, 0, 0, 0, 0);
	if (tid < 0)
		halt_exit(1);

	if (tid == 0)
		child_entry(NULL);

	/* Busy-wait first so we do not depend on nanosleep wake with a sibling. */
	{
		volatile int spin;

		for (spin = 0; spin < 5000000 && !worker_ran; spin++)
			;
	}

	if (!worker_ran)
	{
		struct smoke_timespec ts = { 0, 10000000L };
		int i;

		for (i = 0; i < 500 && !worker_ran; i++)
			ir0_syscall6(SYS_nanosleep, (long)(uintptr_t)&ts, 0, 0, 0, 0, 0);
	}

	if (!worker_ran)
		halt_exit(2);

	if (write(1, "MUSL_PTHREAD_OK\n", 16) != 16)
		halt_exit(3);

	halt_exit(0);
	return 0;
}
