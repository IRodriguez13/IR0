/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fault_signals.c
 * Description: Synchronous CPU faults raise the Linux signal for their class
 *              and reach a user handler when one is installed: illegal
 *              instruction -> SIGILL, breakpoint -> SIGTRAP.
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

#if defined(__x86_64__)
#define FAULT_ILLEGAL_INSN() __asm__ volatile("ud2")
#define FAULT_BREAKPOINT()   __asm__ volatile("int3")
#elif defined(__aarch64__)
#define FAULT_ILLEGAL_INSN() __asm__ volatile("udf #0")
#define FAULT_BREAKPOINT()   __asm__ volatile("brk #0")
#else
#error "fault_signals: unsupported ISA"
#endif

/* The handler never returns: no sigreturn trampoline is needed. */
static void exit_from_handler(int sig)
{
	(void)sig;
	_exit(HANDLER_EXIT_CODE);
}

static int install_handler(int sig)
{
	struct sigaction sa = { 0 };

	sa.sa_handler = exit_from_handler;
	return sigaction(sig, &sa, 0) == 0;
}

/* Run @fault in a child; return its wait status, or -1 on harness error. */
static int child_status(int handler_sig, int which)
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
	{
		if (handler_sig && !install_handler(handler_sig))
			_exit(1);
		if (which == SIGILL)
			FAULT_ILLEGAL_INSN();
		else
			FAULT_BREAKPOINT();
		_exit(0);
	}
	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return -1;
	}
	return status;
}

static int killed_by(int status, int sig)
{
	if (status >= 0 && WIFSIGNALED(status) && WTERMSIG(status) == sig)
		return 1;
	ksft_print_num("wait_status", status);
	return 0;
}

static int exited_from_handler(int status)
{
	if (status >= 0 && WIFEXITED(status) &&
	    WEXITSTATUS(status) == HANDLER_EXIT_CODE)
		return 1;
	ksft_print_num("wait_status", status);
	return 0;
}

int main(void)
{
	ksft_set_plan(4);
	ksft_test_result(killed_by(child_status(0, SIGILL), SIGILL),
			 "illegal_insn_default_sigill");
	ksft_test_result(exited_from_handler(child_status(SIGILL, SIGILL)),
			 "illegal_insn_runs_sigill_handler");
	ksft_test_result(killed_by(child_status(0, SIGTRAP), SIGTRAP),
			 "breakpoint_default_sigtrap");
	ksft_test_result(exited_from_handler(child_status(SIGTRAP, SIGTRAP)),
			 "breakpoint_runs_sigtrap_handler");
	return ksft_exit();
}
