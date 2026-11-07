/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: identity.c
 * Description: getpid + write(1) userspace selftest (dispatch beyond fold-slice).
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

#ifndef SYS_getpid
#ifdef __aarch64__
#define SYS_getpid 172
#else
#define SYS_getpid 39
#endif
#endif

int main(void)
{
	long pid;
	long wr;
	static const char msg[] = "[IDENTITYOK]\n";
	int pass;

	ksft_set_plan(2);

	pid = (long)syscall(SYS_getpid);
	pass = pid > 0;
	ksft_test_result(pass, "getpid_positive");
	if (!pass)
		return ksft_exit();

	wr = (long)write(1, msg, sizeof(msg) - 1U);
	pass = wr == (long)(sizeof(msg) - 1U);
	ksft_test_result(pass, "write_stdout");
	return ksft_exit();
}
