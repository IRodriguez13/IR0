/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: prctl_keepcaps.c
 * Description: PR_SET_KEEPCAPS accepts 0 and 1 and rejects 2.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#if defined(__aarch64__)
#define SYS_prctl 167
#else
#define SYS_prctl 157
#endif
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#define PR_GET_KEEPCAPS 7
#define PR_SET_KEEPCAPS 8

int main(void)
{
	long set1;
	long get1;
	long set0;
	long get0;
	long set2;
	int pass;

	ksft_set_plan(1);
	set1 = syscall(SYS_prctl, PR_SET_KEEPCAPS, 1, 0, 0, 0);
	get1 = syscall(SYS_prctl, PR_GET_KEEPCAPS, 0, 0, 0, 0);
	set0 = syscall(SYS_prctl, PR_SET_KEEPCAPS, 0, 0, 0, 0);
	get0 = syscall(SYS_prctl, PR_GET_KEEPCAPS, 0, 0, 0, 0);
	set2 = syscall(SYS_prctl, PR_SET_KEEPCAPS, 2, 0, 0, 0);
	pass = set1 == 0 && get1 == 1 && set0 == 0 && get0 == 0 && set2 < 0;
	ksft_test_result(pass, "prctl_keepcaps");
	return ksft_exit();
}
