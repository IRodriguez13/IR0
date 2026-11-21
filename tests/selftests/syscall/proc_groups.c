/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: proc_groups.c
 * Description: /proc/self/status reports supplementary groups.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <unistd.h>
#endif

#include "ir0/ksft.h"

static int status_has_line(const char *buf, const char *line)
{
	const char *p;

	p = buf;
	while (*p)
	{
		const char *s;
		const char *w;

		s = p;
		w = line;
		while (*w && *s == *w)
		{
			w++;
			s++;
		}
		if (*w == 0)
		{
			return 1;
		}
		while (*p && *p != '\n')
		{
			p++;
		}
		if (*p == '\n')
		{
			p++;
		}
	}
	return 0;
}

int main(void)
{
	char buf[1024];
	volatile unsigned char *z;
	unsigned int gid;
	int fd;
	int n;
	int i;
	int pass;

	ksft_set_plan(1);
	gid = 7;
#ifdef IR0_SELFTEST_NOLIBC
	/* arch/arm64/include/uapi/asm/unistd.h: __NR_setgroups is 159. */
	if (syscall(SYS_setgroups, 1, (long)&gid) != 0)
#else
	if (setgroups(1, (const gid_t *)&gid) != 0)
#endif
	{
		ksft_test_result(0, "proc_groups");
		return ksft_exit();
	}
	fd = open("/proc/self/status", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "proc_groups");
		return ksft_exit();
	}
	z = (volatile unsigned char *)buf;
	for (i = 0; i < (int)sizeof(buf); i++)
	{
		z[i] = 0;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n < 0)
	{
		n = 0;
	}
	buf[n] = 0;
	pass = status_has_line(buf, "Groups:\t7\n");
	ksft_test_result(pass, "proc_groups");
	return ksft_exit();
}
