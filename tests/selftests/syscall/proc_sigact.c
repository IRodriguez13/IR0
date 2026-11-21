/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: proc_sigact.c
 * Description: /proc/self/status reports ignored and caught signals.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SIGUSR2 12
#define SIG_IGN ((void (*)(int))1)
#else
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

static void on_usr2(int sig)
{
	(void)sig;
}

static int hex_bit(const char *buf, const char *label, int bit)
{
	const char *p;
	unsigned long long word;
	int i;

	p = buf;
	while (*p)
	{
		const char *a = label;
		const char *b = p;

		while (*a && *b == *a)
		{
			a++;
			b++;
		}
		if (*a == 0)
		{
			word = 0;
			for (i = 0; i < 16 && *b; i++, b++)
			{
				unsigned d;

				if (*b >= '0' && *b <= '9')
					d = (unsigned)(*b - '0');
				else if (*b >= 'a' && *b <= 'f')
					d = (unsigned)(*b - 'a' + 10);
				else if (*b >= 'A' && *b <= 'F')
					d = (unsigned)(*b - 'A' + 10);
				else
					return 0;
				word = (word << 4) | d;
			}
			return (word & (1ULL << bit)) != 0;
		}
		p++;
	}
	return 0;
}

int main(void)
{
	struct sigaction sa;
	char buf[1024];
	volatile unsigned char *z;
	int fd;
	int n;
	int i;
	int pass;

	ksft_set_plan(1);
#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sa.sa_mask = 0;
	sa.sa_restorer = 0;
#else
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
#endif
	if (sigaction(SIGUSR1, &sa, 0) != 0)
	{
		ksft_test_result(0, "proc_sigact");
		return ksft_exit();
	}
	sa.sa_handler = on_usr2;
	if (sigaction(SIGUSR2, &sa, 0) != 0)
	{
		ksft_test_result(0, "proc_sigact");
		return ksft_exit();
	}
	fd = open("/proc/self/status", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "proc_sigact");
		return ksft_exit();
	}
	z = (volatile unsigned char *)buf;
	for (i = 0; i < (int)sizeof(buf); i++)
		z[i] = 0;
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n < 0)
		n = 0;
	buf[n] = 0;
	/* Linux bit order: SIGUSR1 is bit 9, SIGUSR2 is bit 11. */
	pass = hex_bit(buf, "SigIgn:\t", 9) && hex_bit(buf, "SigCgt:\t", 11);
	ksft_test_result(pass, "proc_sigact");
	return ksft_exit();
}
