/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: swapon.c
 * Description: swapon(2)/swapoff(2) userspace selftest (errno contract).
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
#ifndef SYS_swapon
#define SYS_swapon __NR_swapon
#endif
#ifndef SYS_swapoff
#define SYS_swapoff __NR_swapoff
#endif
#endif

int main(void)
{
	long ret;
	int pass;

	ksft_set_plan(4);

	ret = syscall(SYS_swapon, (long)"/sbin/ir0swap_noent", 0, 0, 0);
	pass = ret == -1 && (errno == 2 || errno == 1);
	ksft_test_result(pass, "swapon_missing_path");
	if (!pass)
		return ksft_exit();

	ret = syscall(SYS_swapon, (long)"/proc/uptime", 0, 0, 0);
	pass = ret == -1 &&
	       (errno == 95 || errno == 22 || errno == 1);
	ksft_test_result(pass, "swapon_nonswap_file");
	if (!pass)
		return ksft_exit();

	ret = syscall(SYS_swapoff, (long)"/sbin/ir0swap_noent", 0, 0, 0);
	pass = ret == -1 && (errno == 22 || errno == 2 || errno == 1);
	ksft_test_result(pass, "swapoff_inactive");
	if (!pass)
		return ksft_exit();

	ret = syscall(SYS_swapon, 0, 0, 0, 0);
	pass = ret == -1 && (errno == 14 || errno == 1);
	ksft_test_result(pass, "swapon_null_path");
	return ksft_exit();
}
