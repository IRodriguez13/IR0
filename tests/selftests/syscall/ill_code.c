/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ill_code.c
 * Description: An undefined instruction reports the Linux SIGILL si_code.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
/* ARM64 undefined instruction is ILL_ILLOPC. */
#define ILL_EXPECT 1
#else
#include <signal.h>
#include <string.h>
#include <unistd.h>
/* x86 #UD (ud2) is ILL_ILLOPN. */
#define ILL_EXPECT 2
#endif

#include "ir0/ksft.h"

static void on_ill(int sig, siginfo_t *info, void *uc)
{
	(void)sig;
	(void)uc;
	ksft_test_result(info && info->si_code == ILL_EXPECT, "ill_code");
	_exit(ksft_exit());
}

int main(void)
{
	struct sigaction sa;

	ksft_set_plan(1);
#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = (void (*)(int))on_ill;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_ill;
	sa.sa_flags = SA_SIGINFO;
#endif
	if (sigaction(SIGILL, &sa, 0) != 0)
	{
		ksft_test_result(0, "ill_code");
		return ksft_exit();
	}
#ifdef IR0_SELFTEST_NOLIBC
	__asm__ volatile(".inst 0x00000000");
#else
	__asm__ volatile("ud2");
#endif
	ksft_test_result(0, "ill_code");
	return ksft_exit();
}
