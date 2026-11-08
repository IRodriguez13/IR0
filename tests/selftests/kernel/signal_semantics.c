/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: signal_semantics.c
 * Description: Default signal actions follow Linux signal(7): a caught
 *              signal runs its handler, a blocked signal stays pending until
 *              unblocked, and SIGKILL is reported as WIFSIGNALED(SIGKILL).
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

#define HANDLER_EXIT_CODE 42
#define SURVIVED_EXIT_CODE 7

enum child_case
{
	CASE_ABORT_CAUGHT,
	CASE_TERM_BLOCKED,
	CASE_TERM_UNBLOCKED,
	CASE_KILL_SELF,
};

static void exit_from_handler(int sig)
{
	(void)sig;
	_exit(HANDLER_EXIT_CODE);
}

static void block_sigterm(int how)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGTERM);
	sigprocmask(how, &set, 0);
}

static void run_child(enum child_case which)
{
	struct sigaction sa = { 0 };

	switch (which)
	{
	case CASE_ABORT_CAUGHT:
		sa.sa_handler = exit_from_handler;
		if (sigaction(SIGABRT, &sa, 0) != 0)
			_exit(1);
		kill(getpid(), SIGABRT);
		break;
	case CASE_TERM_BLOCKED:
		block_sigterm(SIG_BLOCK);
		kill(getpid(), SIGTERM);
		_exit(SURVIVED_EXIT_CODE);
	case CASE_TERM_UNBLOCKED:
		block_sigterm(SIG_BLOCK);
		kill(getpid(), SIGTERM);
		block_sigterm(SIG_UNBLOCK);
		break;
	case CASE_KILL_SELF:
		kill(getpid(), SIGKILL);
		break;
	}
	_exit(0);
}

static int child_status(enum child_case which)
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
		run_child(which);
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

static int killed_by(int status, int sig)
{
	if (status >= 0 && WIFSIGNALED(status) && WTERMSIG(status) == sig)
		return 1;
	ksft_print_num("wait_status", status);
	return 0;
}

int main(void)
{
	ksft_set_plan(4);
	ksft_test_result(exited_with(child_status(CASE_ABORT_CAUGHT),
				     HANDLER_EXIT_CODE),
			 "caught_sigabrt_runs_handler");
	ksft_test_result(exited_with(child_status(CASE_TERM_BLOCKED),
				     SURVIVED_EXIT_CODE),
			 "blocked_sigterm_stays_pending");
	ksft_test_result(killed_by(child_status(CASE_TERM_UNBLOCKED), SIGTERM),
			 "unblocked_sigterm_terminates");
	ksft_test_result(killed_by(child_status(CASE_KILL_SELF), SIGKILL),
			 "sigkill_reports_wifsignaled");
	return ksft_exit();
}
