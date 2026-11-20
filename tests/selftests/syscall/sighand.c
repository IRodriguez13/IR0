/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sighand.c
 * Description: clone(CLONE_SIGHAND) shares one handler table. Without the
 *              flag the child gets a copy. A later sigaction is visible
 *              across the share and stays private across the copy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#ifndef CLONE_SIGHAND
#define CLONE_SIGHAND 0x800UL
#endif

static void handler_keep(int sig)
{
	(void)sig;
}

static void handler_new(int sig)
{
	(void)sig;
}

static long ir0_clone(unsigned long flags)
{
#ifdef IR0_SELFTEST_NOLIBC
	return syscall(SYS_clone, (long)(flags | SIGCHLD), 0, 0, 0);
#else
	return syscall(SYS_clone, flags | SIGCHLD, 0, 0, 0, 0);
#endif
}

static int install(void (*handler)(int))
{
	struct sigaction sa;

#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = handler;
	sa.sa_flags = 0;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	__builtin_memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
#endif
	return sigaction(SIGUSR1, &sa, 0);
}

static int read_handler(void (**out)(int))
{
	struct sigaction old;

	if (sigaction(SIGUSR1, 0, &old) != 0)
		return -1;
	*out = old.sa_handler;
	return 0;
}

static int reap(int pid, int *status)
{
#ifdef IR0_SELFTEST_NOLIBC
	return wait4(pid, status, 0, 0);
#else
	return waitpid(pid, status, 0);
#endif
}

/*
 * Parent installs handler_keep, the child installs handler_new, then the
 * parent reads its own table. @want is handler_keep for a copy and
 * handler_new when CLONE_SIGHAND shared the table.
 */
static int run_case(unsigned long flags, void (*want)(int))
{
	long pid;
	int status = 0;
	void (*got)(int) = 0;

	if (install(handler_keep) != 0)
		return 0;
	pid = ir0_clone(flags);
	if (pid < 0)
	{
		ksft_print_num("clone_errno", errno);
		return 0;
	}
	if (pid == 0)
	{
		if (install(handler_new) != 0)
			_exit(2);
		_exit(0);
	}
	if (reap((int)pid, &status) < 0)
		return 0;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		ksft_print_num("child_status", status);
		return 0;
	}
	if (read_handler(&got) != 0)
		return 0;
	return got == want;
}

int main(void)
{
	ksft_set_plan(2);
	ksft_test_result(run_case(0, handler_keep), "sighand_copied");
	ksft_test_result(run_case(CLONE_SIGHAND, handler_new), "sighand_shared");
	return ksft_exit();
}
