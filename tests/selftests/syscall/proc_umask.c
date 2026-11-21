/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: proc_umask.c
 * Description: /proc/self/status reports the task umask.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SYS_umask 166
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
	char buf[1024];
	volatile unsigned char *zero;
	unsigned int i;
	int fd;
	ssize_t n;
	int pass;

	ksft_set_plan(1);
	if (syscall(SYS_umask, 022) < 0)
	{
		ksft_test_result(0, "proc_umask");
		return ksft_exit();
	}
	zero = (volatile unsigned char *)buf;
	for (i = 0; i < sizeof(buf); i++)
		zero[i] = 0;

	fd = open("/proc/self/status", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "proc_umask");
		return ksft_exit();
	}
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	pass = contains(buf, "Umask:\t0022\n");
	ksft_test_result(pass, "proc_umask");
	return ksft_exit();
}
