/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: proc_status.c
 * Description: /proc/self/status carries TracerPid, Threads and NoNewPrivs.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#define STATUS_PATH "/proc/self/status"

static int contains(const char *buf, const char *needle)
{
	unsigned int i;
	unsigned int j;

	if (!buf || !needle)
		return 0;
	for (i = 0; buf[i]; i++)
	{
		for (j = 0; needle[j] && buf[i + j] == needle[j]; j++)
			;
		if (!needle[j])
			return 1;
	}
	return 0;
}

int main(void)
{
	char buf[1024];
	volatile unsigned char *zero;
	int fd;
	ssize_t n;
	unsigned int i;
	int pass;

	ksft_set_plan(1);
	zero = (volatile unsigned char *)buf;
	for (i = 0; i < sizeof(buf); i++)
		zero[i] = 0;

	fd = open(STATUS_PATH, O_RDONLY);
	if (fd < 0)
	{
		ksft_test_result(0, "proc_status");
		return ksft_exit();
	}
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	pass = contains(buf, "TracerPid:\t0\n") &&
	       contains(buf, "Threads:\t1\n") &&
	       contains(buf, "NoNewPrivs:\t0\n");
	ksft_test_result(pass, "proc_status");
	return ksft_exit();
}
