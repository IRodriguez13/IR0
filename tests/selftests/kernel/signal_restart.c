/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: signal_restart.c
 * Description: A caught signal ends a blocked syscall as on Linux
 *              (signal(7) "Interruption of system calls"): read and wait4
 *              restart when the handler has SA_RESTART and fail with EINTR
 *              when it has not; nanosleep always fails with EINTR and
 *              reports the time it had left.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

/* errno(3) EINTR and sigaction(2) SA_RESTART; nolibc has neither header. */
#define LINUX_EINTR 4
#define LINUX_SA_RESTART 0x10000000
#define SIGNAL_DELAY_NS 50000000L
#define CHILD_EXIT_CODE 5
#define LONG_SLEEP_S 2L

static volatile int g_handler_runs;

static void on_sigusr1(int sig)
{
	(void)sig;
	g_handler_runs++;
}

static void sleep_ns(long ns)
{
	struct timespec ts = { 0, ns };

	nanosleep(&ts, 0);
}

static int catch_sigusr1(unsigned long flags)
{
	struct sigaction sa = { 0 };

	sa.sa_handler = on_sigusr1;
	sa.sa_flags = flags;
	g_handler_runs = 0;
	return sigaction(SIGUSR1, &sa, 0) == 0;
}

static int reap(int pid, int code)
{
	int status = 0;

	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("reap_errno", errno);
		return 0;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != code)
	{
		ksft_print_num("reap_status", status);
		return 0;
	}
	return 1;
}

/*
 * Child: signal the parent while it is blocked, wait again, then optionally
 * write one byte to @fd and exit with CHILD_EXIT_CODE.
 */
static int fork_signaller(int parent, int fd)
{
	int pid = fork();

	if (pid != 0)
		return pid;
	sleep_ns(SIGNAL_DELAY_NS);
	kill(parent, SIGUSR1);
	sleep_ns(SIGNAL_DELAY_NS);
	if (fd >= 0 && write(fd, "x", 1) != 1)
		_exit(1);
	_exit(CHILD_EXIT_CODE);
	return 0;
}

/* SA_RESTART: the read continues after the handler and gets the byte. */
static int read_restarts_with_sa_restart(void)
{
	int fds[2];
	char c = 0;
	ssize_t n;
	int err;
	int pid;

	if (!catch_sigusr1(LINUX_SA_RESTART) || pipe2(fds, 0) != 0)
		return 0;
	pid = fork_signaller(getpid(), fds[1]);
	if (pid < 0)
		return 0;
	n = read(fds[0], &c, 1);
	err = errno;
	close(fds[0]);
	close(fds[1]);
	if (n != 1 || c != 'x' || g_handler_runs != 1)
	{
		ksft_print_num("read_ret", (long)n);
		ksft_print_num("read_errno", err);
		ksft_print_num("handler_runs", g_handler_runs);
		reap(pid, CHILD_EXIT_CODE);
		return 0;
	}
	return reap(pid, CHILD_EXIT_CODE);
}

/* No SA_RESTART: wait4 fails with EINTR and the child is still waitable. */
static int wait4_eintr_without_sa_restart(void)
{
	int status = 0;
	int ret;
	int err;
	int pid;

	if (!catch_sigusr1(0))
		return 0;
	pid = fork_signaller(getpid(), -1);
	if (pid < 0)
		return 0;
	ret = wait4(pid, &status, 0, 0);
	err = errno;
	if (ret != -1 || err != LINUX_EINTR || g_handler_runs != 1)
	{
		ksft_print_num("wait4_ret", ret);
		ksft_print_num("wait4_errno", err);
		ksft_print_num("handler_runs", g_handler_runs);
		if (ret == pid)
			return 0;
		reap(pid, CHILD_EXIT_CODE);
		return 0;
	}
	return reap(pid, CHILD_EXIT_CODE);
}

/* SA_RESTART: wait4 continues after the handler and reaps the child. */
static int wait4_restarts_with_sa_restart(void)
{
	int status = 0;
	int ret;
	int err;
	int pid;

	if (!catch_sigusr1(LINUX_SA_RESTART))
		return 0;
	pid = fork_signaller(getpid(), -1);
	if (pid < 0)
		return 0;
	ret = wait4(pid, &status, 0, 0);
	err = errno;
	if (ret != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != CHILD_EXIT_CODE || g_handler_runs != 1)
	{
		ksft_print_num("wait4_ret", ret);
		ksft_print_num("wait4_errno", err);
		ksft_print_num("wait4_status", status);
		ksft_print_num("handler_runs", g_handler_runs);
		if (ret != pid)
			reap(pid, CHILD_EXIT_CODE);
		return 0;
	}
	return 1;
}

/*
 * nanosleep is never restarted (signal(7)), even with SA_RESTART: it fails
 * with EINTR and stores the unslept time, which must be less than asked.
 */
static int nanosleep_eintr_reports_remaining(void)
{
	struct timespec req = { LONG_SLEEP_S, 0 };
	struct timespec rem = { -1, -1 };
	long left;
	int ret;
	int err;
	int pid;

	if (!catch_sigusr1(LINUX_SA_RESTART))
		return 0;
	pid = fork_signaller(getpid(), -1);
	if (pid < 0)
		return 0;
	ret = nanosleep(&req, &rem);
	err = errno;
	left = rem.tv_sec * 1000000000L + rem.tv_nsec;
	if (ret != -1 || err != LINUX_EINTR || g_handler_runs != 1 ||
	    rem.tv_nsec < 0 || rem.tv_nsec >= 1000000000L || left <= 0 ||
	    left >= LONG_SLEEP_S * 1000000000L)
	{
		ksft_print_num("nanosleep_ret", ret);
		ksft_print_num("nanosleep_errno", err);
		ksft_print_num("rem_sec", rem.tv_sec);
		ksft_print_num("rem_nsec", rem.tv_nsec);
		ksft_print_num("handler_runs", g_handler_runs);
		reap(pid, CHILD_EXIT_CODE);
		return 0;
	}
	return reap(pid, CHILD_EXIT_CODE);
}

int main(void)
{
	ksft_set_plan(4);
	ksft_test_result(read_restarts_with_sa_restart(),
			 "read_restarts_with_sa_restart");
	ksft_test_result(wait4_eintr_without_sa_restart(),
			 "wait4_eintr_without_sa_restart");
	ksft_test_result(wait4_restarts_with_sa_restart(),
			 "wait4_restarts_with_sa_restart");
	ksft_test_result(nanosleep_eintr_reports_remaining(),
			 "nanosleep_eintr_reports_remaining");
	return ksft_exit();
}
