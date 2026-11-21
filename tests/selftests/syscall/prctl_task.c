/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: prctl_task.c
 * Description: PR_SET_NAME round-trip and PR_SET_PDEATHSIG when the
 *              parent exits.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#if defined(__aarch64__)
#define SYS_prctl 167
#define SYS_nanosleep 101
#else
#define SYS_prctl 157
#define SYS_nanosleep 35
#endif
#else
#define _GNU_SOURCE
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#define PR_SET_PDEATHSIG 1
#define PR_GET_PDEATHSIG 2
#define PR_SET_NAME 15
#define PR_GET_NAME 16

static volatile int got_usr1;

static void on_usr1(int sig)
{
	(void)sig;
	got_usr1 = 1;
}

static int same_name(const char *got, const char *want)
{
	while (*want)
	{
		if (*got != *want)
			return 0;
		got++;
		want++;
	}
	return *got == 0;
}

static int name_roundtrip(void)
{
	char in[16];
	char out[16];
	unsigned int i;
	const char *want = "ir0-prctl";

	for (i = 0; i < sizeof(in); i++)
		in[i] = 0;
	for (i = 0; want[i] && i + 1 < sizeof(in); i++)
		in[i] = want[i];
	if (syscall(SYS_prctl, PR_SET_NAME, (long)in, 0L, 0L, 0L) != 0)
		return 0;
	for (i = 0; i < sizeof(out); i++)
		out[i] = 0;
	if (syscall(SYS_prctl, PR_GET_NAME, (long)out, 0L, 0L, 0L) != 0)
		return 0;
	return same_name(out, want);
}

static int pdeathsig_from_parent(void)
{
	int ready[2];
	int done[2];
	int middle;
	int grand;
	char byte;
	struct
	{
		long tv_sec;
		long tv_nsec;
	} req;

	if (pipe2(ready, 0) != 0 || pipe2(done, 0) != 0)
		return 0;

	middle = fork();
	if (middle < 0)
		return 0;
	if (middle == 0)
	{
		grand = fork();
		if (grand < 0)
			_exit(1);
		if (grand == 0)
		{
			struct sigaction sa;

#ifdef IR0_SELFTEST_NOLIBC
			sa.sa_handler = on_usr1;
			sa.sa_flags = 0;
			sa.sa_restorer = 0;
			sa.sa_mask = 0;
#else
			memset(&sa, 0, sizeof(sa));
			sa.sa_handler = on_usr1;
#endif
			if (sigaction(SIGUSR1, &sa, 0) != 0)
				_exit(1);
			if (syscall(SYS_prctl, PR_SET_PDEATHSIG, SIGUSR1,
				    0L, 0L, 0L) != 0)
				_exit(1);
			byte = 'G';
			if (write(ready[1], &byte, 1) != 1)
				_exit(1);
			req.tv_sec = 2;
			req.tv_nsec = 0;
			(void)syscall(SYS_nanosleep, (long)&req, 0L);
			byte = got_usr1 ? '1' : '0';
			(void)write(done[1], &byte, 1);
			_exit(0);
		}
		if (read(ready[0], &byte, 1) != 1)
			_exit(1);
		_exit(0);
	}

	if (read(done[0], &byte, 1) != 1)
		return 0;
	(void)wait4(middle, 0, 0, 0);
	return byte == '1';
}

int main(void)
{
	ksft_set_plan(2);
	ksft_test_result(name_roundtrip(), "prctl_name");
	ksft_test_result(pdeathsig_from_parent(), "prctl_pdeathsig");
	return ksft_exit();
}
