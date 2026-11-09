/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: blocking_sleep.c
 * Description: Blocking syscalls sleep in the kernel and continue when they
 *              are woken, as on Linux: a pipe read waits for a writer,
 *              wait4 waits for a child to exit, nanosleep waits at least
 *              the requested time, and a caught signal ends a blocked read
 *              with EINTR.
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

/* errno(3) EINTR; nolibc has no <errno.h>. */
#define LINUX_EINTR 4
#define CHILD_DELAY_NS 50000000L
#define SLEEP_NS 20000000L
#define CHILD_EXIT_CODE 5

static volatile int g_sigusr1_seen;

static void on_sigusr1(int sig)
{
	(void)sig;
	g_sigusr1_seen = 1;
}

static void sleep_ns(long ns)
{
	struct timespec ts = { 0, ns };

	nanosleep(&ts, 0);
}

static long now_ns(void)
{
	struct timespec ts = { 0, 0 };

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return -1;
	return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static int reap(int pid, int code)
{
	int status = 0;

	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return 0;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != code)
	{
		ksft_print_num("wait_status", status);
		return 0;
	}
	return 1;
}

/* The reader blocks on an empty pipe until the child writes. */
static int pipe_read_waits_for_writer(void)
{
	int fds[2];
	char c = 0;
	ssize_t n;
	int pid;

	if (pipe2(fds, 0) != 0)
		return 0;
	pid = fork();
	if (pid < 0)
		return 0;
	if (pid == 0)
	{
		close(fds[0]);
		sleep_ns(CHILD_DELAY_NS);
		_exit(write(fds[1], "x", 1) == 1 ? 0 : 1);
	}
	close(fds[1]);
	n = read(fds[0], &c, 1);
	close(fds[0]);
	if (n != 1 || c != 'x')
	{
		ksft_print_num("read_ret", (long)n);
		ksft_print_num("read_errno", errno);
		reap(pid, 0);
		return 0;
	}
	return reap(pid, 0);
}

/* wait4 blocks until the child exits and reports its status. */
static int wait4_waits_for_child(void)
{
	int pid = fork();

	if (pid < 0)
		return 0;
	if (pid == 0)
	{
		sleep_ns(CHILD_DELAY_NS);
		_exit(CHILD_EXIT_CODE);
	}
	return reap(pid, CHILD_EXIT_CODE);
}

/* nanosleep returns 0 after at least the requested interval. */
static int nanosleep_waits(void)
{
	struct timespec ts = { 0, SLEEP_NS };
	long start = now_ns();
	long end;
	int ret;

	ret = nanosleep(&ts, 0);
	end = now_ns();
	if (ret != 0 || start < 0 || end < 0 || end - start < SLEEP_NS)
	{
		ksft_print_num("nanosleep_ret", ret);
		ksft_print_num("elapsed_ns", end - start);
		return 0;
	}
	return 1;
}

/* A caught signal (no SA_RESTART) ends a blocked read with EINTR. */
static int signal_interrupts_read(void)
{
	struct sigaction sa = { 0 };
	int fds[2];
	char c = 0;
	ssize_t n;
	int err;
	int pid;
	int parent = getpid();

	sa.sa_handler = on_sigusr1;
	if (sigaction(SIGUSR1, &sa, 0) != 0)
		return 0;
	if (pipe2(fds, 0) != 0)
		return 0;
	pid = fork();
	if (pid < 0)
		return 0;
	if (pid == 0)
	{
		sleep_ns(CHILD_DELAY_NS);
		kill(parent, SIGUSR1);
		_exit(0);
	}
	n = read(fds[0], &c, 1);
	err = errno;
	close(fds[0]);
	close(fds[1]);
	if (n != -1 || err != LINUX_EINTR || !g_sigusr1_seen)
	{
		ksft_print_num("read_ret", (long)n);
		ksft_print_num("read_errno", err);
		ksft_print_num("handler_ran", g_sigusr1_seen);
		reap(pid, 0);
		return 0;
	}
	return reap(pid, 0);
}

int main(void)
{
	ksft_set_plan(4);
	ksft_test_result(pipe_read_waits_for_writer(),
			 "pipe_read_waits_for_writer");
	ksft_test_result(wait4_waits_for_child(), "wait4_waits_for_child");
	ksft_test_result(nanosleep_waits(), "nanosleep_waits");
	ksft_test_result(signal_interrupts_read(), "signal_interrupts_read");
	return ksft_exit();
}
