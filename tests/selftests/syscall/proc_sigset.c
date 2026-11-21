/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: proc_sigset.c
 * Description: /proc/self/status reports SigPnd and SigBlk in Linux bit order.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SYS_rt_sigprocmask 135
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

#include "ir0/ksft.h"

static int contains(const char *buf, const char *needle)
{
	size_t i;
	size_t n;

	if (!buf || !needle)
		return 0;
	n = 0;
	while (needle[n])
		n++;
	for (i = 0; buf[i]; i++)
	{
		size_t j;

		for (j = 0; j < n && buf[i + j] == needle[j]; j++)
			;
		if (j == n)
			return 1;
	}
	return 0;
}

int main(void)
{
	unsigned long set;
	char buf[1024];
	volatile unsigned char *p;
	int fd;
	int n;
	int pass;

	ksft_set_plan(1);
	/* SIGUSR1 is signal 10. Linux sigset bit 9. SIG_BLOCK is 0. */
	set = 1ul << 9;
	if (syscall(SYS_rt_sigprocmask, 0, &set, 0, 8) < 0)
	{
		ksft_test_result(0, "proc_sigset");
		return ksft_exit();
	}
	fd = open("/proc/self/status", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "proc_sigset");
		return ksft_exit();
	}
	p = (volatile unsigned char *)buf;
	for (n = 0; n < (int)sizeof(buf); n++)
		p[n] = 0;
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	pass = contains(buf, "SigBlk:\t0000000000000200\n") &&
	       contains(buf, "SigPnd:\t0000000000000000\n");
	ksft_test_result(pass, "proc_sigset");
	return ksft_exit();
}
