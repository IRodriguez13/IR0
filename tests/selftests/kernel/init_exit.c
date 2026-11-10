/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_exit.c
 * Description: PID 1 returns from main() with status 42. As on Linux, the
 *              death of init panics the kernel with
 *              "Attempted to kill init! exitcode=0x00002a00" (the wait
 *              status of exit(42)); the runner expects that line.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <unistd.h>
#endif

#define INIT_EXIT_STATUS 42

int main(void)
{
	ksft_set_plan(1);
	ksft_test_result(getpid() == 1, "selftest_runs_as_init");
	/* The kernel must panic on this exit instead of idling. */
	return INIT_EXIT_STATUS;
}
