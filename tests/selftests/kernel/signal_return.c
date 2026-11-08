/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: signal_return.c
 * Description: A caught signal handler that returns resumes the interrupted
 *              code through rt_sigreturn: with SA_RESTORER the handler
 *              returns into the caller's trampoline on every ISA, and
 *              rt_sigreturn restores the pre-handler signal mask so the
 *              same signal can be caught again.
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
#include <unistd.h>
#endif

#define RESUMED_EXIT_CODE 0
#define NOT_RUN_EXIT_CODE 3
#define SIGACTION_EXIT_CODE 4
#define KILL_EXIT_CODE 5
#define MASK_EXIT_CODE 6

static volatile int handler_runs;

static void count_and_return(int sig)
{
	(void)sig;
	handler_runs++;
}

static int install_with_libc(int sig)
{
	struct sigaction sa = { 0 };

	sa.sa_handler = count_and_return;
	return sigaction(sig, &sa, 0) == 0;
}

/* The handler blocks @sig while it runs; rt_sigreturn must unblock it. */
static int still_blocked(int sig)
{
	sigset_t cur;

	sigemptyset(&cur);
	if (sigprocmask(SIG_BLOCK, 0, &cur) != 0)
		return 1;
	return sigismember(&cur, sig) != 0;
}

static void run_child(void)
{
	int round;

	if (!install_with_libc(SIGUSR1))
		_exit(SIGACTION_EXIT_CODE);
	/*
	 * Code after kill() runs only once rt_sigreturn restored the
	 * interrupted context; a second round proves the restored mask.
	 */
	for (round = 1; round <= 2; round++)
	{
		if (kill(getpid(), SIGUSR1) != 0)
			_exit(KILL_EXIT_CODE);
		if (handler_runs != round)
			_exit(NOT_RUN_EXIT_CODE);
		if (still_blocked(SIGUSR1))
			_exit(MASK_EXIT_CODE);
	}
	_exit(RESUMED_EXIT_CODE);
}

static int child_status(void)
{
	int status = 0;
	int pid;

	pid = fork();
	if (pid < 0)
	{
		ksft_print_num("fork_errno", errno);
		return -1;
	}
	if (pid == 0)
		run_child();
	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return -1;
	}
	return status;
}

static int exited_with(int status, int code)
{
	if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == code)
		return 1;
	ksft_print_num("wait_status", status);
	return 0;
}

int main(void)
{
	ksft_set_plan(1);
	ksft_test_result(exited_with(child_status(), RESUMED_EXIT_CODE),
			 "handler_returns_through_sa_restorer");
	return ksft_exit();
}
