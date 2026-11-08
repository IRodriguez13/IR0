/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: panic_nested.c
 * Description: Force a kernel page fault inside the panic path through
 *              /sys/kernel/panic ("nested"). The runner expects panicex's
 *              nested branch (PANICEX_DOUBLE_FAULT_SAFE_OK): a fault while
 *              panicking must stop there, not replay the faulting
 *              instruction or start a second banner.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <fcntl.h>
#include <unistd.h>
#endif

int main(void)
{
	static const char cmd[] = "nested";
	int fd;

	ksft_set_plan(1);
	fd = open("/sys/kernel/panic", O_WRONLY);
	if (fd < 0)
	{
		ksft_test_result(0, "open_sys_kernel_panic");
		return ksft_exit();
	}
	/* Does not return when the kernel honours the request. */
	(void)write(fd, cmd, sizeof(cmd) - 1U);
	ksft_test_result(0, "panic_did_not_happen");
	return ksft_exit();
}
