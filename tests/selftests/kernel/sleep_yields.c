/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sleep_yields.c
 * Description: A task sleeping in the kernel gives the CPU away until it is
 *              woken, as on Linux: timed sleeps (nanosleep, a ppoll timeout)
 *              charge the sleeper no CPU time. A sleeper that halted in
 *              place instead took a timer tick per sleep, which
 *              /proc/self/stat reports as utime + stime.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef IR0_SELFTEST_NOLIBC
#define _GNU_SOURCE
#endif

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#endif

#define SLEEPS 40
#define SLEEP_NS 5000000L
/* CPU time is in scheduler ticks; a halting sleeper takes at least SLEEPS. */
#define MAX_CPU_TICKS 10

/* Fields 14 + 15 (utime + stime) of /proc/self/stat; -1 if unreadable. */
static long self_cpu_ticks(void)
{
	char buf[512];
	ssize_t n;
	long value = 0;
	long utime = 0;
	int field = 2;
	int fd;
	ssize_t i;
	ssize_t paren = -1;

	fd = open("/proc/self/stat", O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	for (i = 0; i < n; i++)
	{
		if (buf[i] == ')')
			paren = i;
	}
	if (paren < 0)
		return -1;
	for (i = paren + 1; i < n && field < 14; i++)
	{
		if (buf[i] == ' ')
			field++;
	}
	if (field != 14)
		return -1;
	for (; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
		utime = utime * 10 + (buf[i] - '0');
	if (i >= n || buf[i] != ' ')
		return -1;
	for (i++; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
		value = value * 10 + (buf[i] - '0');
	return utime + value;
}

static int cpu_within_budget(long before, long after)
{
	if (before < 0 || after < 0 || after - before > MAX_CPU_TICKS)
	{
		ksft_print_num("cpu_ticks_before", before);
		ksft_print_num("cpu_ticks_after", after);
		return 0;
	}
	ksft_print_num("cpu_ticks", after - before);
	return 1;
}

/* SLEEPS nanosleep calls: the clock-wait path. */
static int nanosleep_charges_no_cpu(void)
{
	struct timespec ts = { 0, SLEEP_NS };
	long before = self_cpu_ticks();
	int i;

	for (i = 0; i < SLEEPS; i++)
	{
		if (nanosleep(&ts, 0) != 0)
			return 0;
	}
	return cpu_within_budget(before, self_cpu_ticks());
}

/* SLEEPS ppoll timeouts on an empty pipe: the poll-wait path. */
static int ppoll_timeout_charges_no_cpu(void)
{
	struct timespec ts = { 0, SLEEP_NS };
	struct pollfd pfd;
	int fds[2];
	long before;
	int ok = 1;
	int i;

	if (pipe2(fds, 0) != 0)
		return 0;
	before = self_cpu_ticks();
	for (i = 0; i < SLEEPS && ok; i++)
	{
		pfd.fd = fds[0];
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (ppoll(&pfd, 1, &ts, 0) != 0)
			ok = 0;
	}
	if (ok)
		ok = cpu_within_budget(before, self_cpu_ticks());
	close(fds[0]);
	close(fds[1]);
	return ok;
}

int main(void)
{
	ksft_set_plan(2);
	ksft_test_result(nanosleep_charges_no_cpu(), "nanosleep_charges_no_cpu");
	ksft_test_result(ppoll_timeout_charges_no_cpu(),
			 "ppoll_timeout_charges_no_cpu");
	return ksft_exit();
}
