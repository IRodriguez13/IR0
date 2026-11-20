/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: nofile.c
 * Description: RLIMIT_NOFILE grows past the old 64-slot table, then EMFILE.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* errno(3) EMFILE; nolibc has no <errno.h>. */
#define LINUX_EMFILE 24

#define RLIMIT_NOFILE 7

#ifndef SYS_dup
#ifdef __aarch64__
#define SYS_dup 23
#else
#define SYS_dup 32
#endif
#endif

#ifndef SYS_prlimit64
#ifdef __aarch64__
#define SYS_prlimit64 261
#else
#define SYS_prlimit64 302
#endif
#endif

struct ir0_rlimit
{
	unsigned long rlim_cur;
	unsigned long rlim_max;
};

static long ir0_dup(int fd)
{
	return (long)syscall(SYS_dup, fd);
}

static long ir0_prlimit(const struct ir0_rlimit *neu, struct ir0_rlimit *old)
{
	return (long)syscall(SYS_prlimit64, 0, RLIMIT_NOFILE, neu, old);
}

int main(void)
{
	struct ir0_rlimit rl;
	struct ir0_rlimit lowered;
	int high;
	int n;
	long d;
	int pass;

	ksft_set_plan(3);

	pass = ir0_prlimit(0, &rl) == 0 && rl.rlim_cur >= 128;
	ksft_test_result(pass, "nofile_soft_ge_128");
	if (!pass)
		return ksft_exit();

	high = -1;
	for (n = 0; n < 96; n++)
	{
		d = ir0_dup(1);
		if (d < 0)
			break;
		high = (int)d;
		if (high >= 80)
			break;
	}
	pass = high >= 80;
	ksft_test_result(pass, "dup_past_64");
	if (!pass)
		return ksft_exit();

	lowered = rl;
	lowered.rlim_cur = (unsigned long)high + 1UL;
	pass = ir0_prlimit(&lowered, 0) == 0 && ir0_dup(1) < 0 &&
	       errno == LINUX_EMFILE;
	ksft_test_result(pass, "emfile_at_soft");
	return ksft_exit();
}
