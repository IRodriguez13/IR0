/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: proc_comm.c
 * Description: /proc/self/comm returns the task comm from PR_SET_NAME.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

static int is_ir0comm(const char *buf, int n)
{
	const char *want = "ir0comm\n";
	int i;

	if (n != 8)
	{
		return 0;
	}
	for (i = 0; i < n; i++)
	{
		if (buf[i] != want[i])
		{
			return 0;
		}
	}
	return 1;
}

int main(void)
{
	char buf[64];
	volatile unsigned char *z;
	int fd;
	int n;
	int i;
	int pass;

	ksft_set_plan(1);
#ifdef IR0_SELFTEST_NOLIBC
	/* arch/arm64/include/uapi/asm/unistd.h: __NR_prctl is 167. */
	if (syscall(167, 15, (long)"ir0comm", 0L, 0L) != 0)
#else
	if (prctl(PR_SET_NAME, "ir0comm") != 0)
#endif
	{
		ksft_test_result(0, "proc_comm");
		return ksft_exit();
	}
	fd = open("/proc/self/comm", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "proc_comm");
		return ksft_exit();
	}
	z = (volatile unsigned char *)buf;
	for (i = 0; i < (int)sizeof(buf); i++)
	{
		z[i] = 0;
	}
	n = (int)read(fd, buf, sizeof(buf));
	(void)close(fd);
	if (n < 0)
	{
		n = 0;
	}
	pass = is_ir0comm(buf, n);
	ksft_test_result(pass, "proc_comm");
	return ksft_exit();
}
