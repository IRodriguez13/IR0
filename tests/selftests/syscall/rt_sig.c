/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rt_sig.c
 * Description: Signal 34 (real-time, inside SIGRTMIN..SIGRTMAX) is installed
 *              with rt_sigaction and delivered by kill and tgkill.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define RT_TGKILL 131L
#else
#define _GNU_SOURCE
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#define RT_TGKILL SYS_tgkill
#endif

#include "ir0/ksft.h"

/* Linux real-time signal inside 32..63. Not SIGRTMIN from libc. */
#define RT_SIG_NUM 34
#define RT_SA_RESTORER 0x04000000UL

/*
 * Kernel rt_sigaction layout when sigsetsize is 8. libc sigaction() rejects
 * signal 34 (it keeps 32..34 for itself), so the test calls the syscall.
 */
struct rt_ksa
{
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned long mask;
};

#if defined(__x86_64__)
__asm__(
	".global rt_sig_restore\n"
	"rt_sig_restore:\n"
	"\tmov $15, %rax\n"
	"\tsyscall\n"
);
#elif defined(__aarch64__)
__asm__(
	".global rt_sig_restore\n"
	"rt_sig_restore:\n"
	"\tmov x8, #139\n"
	"\tsvc #0\n"
);
#endif

extern void rt_sig_restore(void);

static volatile int rt_hits;

static void on_rt(int sig)
{
	if (sig == RT_SIG_NUM)
		rt_hits++;
}

static int rt_action(int sig, void (*handler)(int))
{
	struct rt_ksa sa;

	sa.handler = handler;
	sa.flags = RT_SA_RESTORER;
	sa.restorer = rt_sig_restore;
	sa.mask = 0;
	if (syscall(SYS_rt_sigaction, sig, (long)&sa, 0L, 8L) != 0)
	{
		ksft_print_num("rt_sigaction_errno", errno);
		return -1;
	}
	return 0;
}

static int deliver_34(void)
{
	long tid;
	long pid;

	if (rt_action(RT_SIG_NUM, on_rt) != 0)
		return 0;

	rt_hits = 0;
	pid = syscall(SYS_getpid);
	if (pid <= 0)
	{
		ksft_print_num("getpid_errno", errno);
		return 0;
	}
	if (syscall(SYS_kill, pid, RT_SIG_NUM) != 0)
	{
		ksft_print_num("kill_errno", errno);
		return 0;
	}
	if (rt_hits != 1)
		return 0;

	tid = syscall(SYS_gettid);
	if (tid <= 0)
	{
		ksft_print_num("gettid_errno", errno);
		return 0;
	}
	if (syscall(RT_TGKILL, pid, tid, RT_SIG_NUM) != 0)
	{
		ksft_print_num("tgkill_errno", errno);
		return 0;
	}
	return rt_hits == 2;
}

int main(void)
{
	ksft_set_plan(1);
	ksft_test_result(deliver_34(), "rt_sig_34_delivered");
	return ksft_exit();
}
