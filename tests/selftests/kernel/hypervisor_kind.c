/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: hypervisor_kind.c
 * Description: /proc/cpuinfo hypervisor_kind line under a hypervisor.
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

static int starts_with(const char *s, const char *prefix)
{
	while (*prefix != '\0')
	{
		if (*s != *prefix)
			return 0;
		s++;
		prefix++;
	}
	return 1;
}

static int kind_token(const char *value, const char *want)
{
	while (*want != '\0')
	{
		if (*value != *want)
			return 0;
		value++;
		want++;
	}
	return *value == '\n' || *value == '\0';
}

int main(void)
{
	char buf[2048];
	char *p;
	long n;
	int fd;
	int pass = 0;

	ksft_set_plan(1);
	fd = open("/proc/cpuinfo", O_RDONLY);
	if (fd < 0)
	{
		ksft_test_result(0, "hypervisor_kind");
		return ksft_exit();
	}
	n = (long)read(fd, buf, sizeof(buf) - 1U);
	(void)close(fd);
	if (n < 0)
		n = 0;
	buf[n] = '\0';

	p = buf;
	while (*p != '\0')
	{
		if (starts_with(p, "hypervisor_kind\t"))
		{
			const char *value = p + 16;

			pass = kind_token(value, "kvm") ||
			       kind_token(value, "xen") ||
			       kind_token(value, "other");
			ksft_write_lit("# hypervisor_kind=");
			while (*value != '\0' && *value != '\n')
			{
				(void)write(1, value, 1);
				value++;
			}
			ksft_write_lit("\n");
			break;
		}
		while (*p != '\0' && *p != '\n')
			p++;
		if (*p == '\n')
			p++;
	}

	ksft_test_result(pass, "hypervisor_kind");
	return ksft_exit();
}
