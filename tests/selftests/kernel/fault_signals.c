/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fault_signals.c
 * Description: Synchronous CPU faults raise the Linux signal for their class
 *              and reach a user handler when one is installed: illegal
 *              instruction -> SIGILL, breakpoint -> SIGTRAP. A SIGSEGV
 *              handler that makes the page writable and returns resumes
 *              the faulting store through rt_sigreturn, as on Linux.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define HANDLER_EXIT_CODE 42
#define RESUMED_EXIT_CODE 0
#define SETUP_EXIT_CODE 3
#define SIGINFO_EXIT_CODE 4
#define MPROTECT_EXIT_CODE 5
#define NOT_RESUMED_EXIT_CODE 6
#define FAULT_PAGE_SIZE 4096UL
#define FAULT_STORE_VALUE 0x5a

#if defined(__x86_64__)
#define FAULT_ILLEGAL_INSN() __asm__ volatile("ud2")
#define FAULT_BREAKPOINT()   __asm__ volatile("int3")

static int store_byte_resumed(volatile unsigned char *p, unsigned char v)
{
	*p = v;
	return 1;
}
#elif defined(__aarch64__)
#define FAULT_ILLEGAL_INSN() __asm__ volatile("udf #0")
#define FAULT_BREAKPOINT()   __asm__ volatile("brk #0")

/*
 * Point x30 at a trap label before the store: a handler that "returns"
 * through the faulting context's link register instead of rt_sigreturn
 * lands there. Only a restored PC reaches the instruction after the store.
 */
static int store_byte_resumed(volatile unsigned char *p, unsigned char v)
{
	unsigned long state;

	__asm__ volatile("adr x30, 1f\n\t"
			 "strb %w[v], [%[p]]\n\t"
			 "mov %[s], #1\n\t"
			 "b 2f\n"
			 "1:\tmov %[s], #2\n"
			 "2:"
			 : [s] "=&r"(state)
			 : [p] "r"(p), [v] "r"((unsigned long)v)
			 : "x30", "memory");
	return state == 1;
}
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

static volatile unsigned char *segv_page;
static volatile int segv_handler_runs;

/*
 * Linux: the handler of a synchronous fault returns through rt_sigreturn to
 * the faulting instruction, which runs again. Make the page writable so
 * the retried store succeeds.
 */
static void unprotect_and_return(int sig, siginfo_t *info, void *uc)
{
	(void)uc;
	segv_handler_runs++;
	if (sig != SIGSEGV || !info || info->si_signo != SIGSEGV ||
	    info->si_addr != (void *)segv_page)
		_exit(SIGINFO_EXIT_CODE);
	if (mprotect((void *)segv_page, FAULT_PAGE_SIZE,
		     PROT_READ | PROT_WRITE) != 0)
		_exit(MPROTECT_EXIT_CODE);
}

static void store_to_readonly_page(void)
{
	struct sigaction sa = { 0 };

	segv_page = mmap(0, FAULT_PAGE_SIZE, PROT_READ,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (segv_page == MAP_FAILED)
		_exit(SETUP_EXIT_CODE);
	/* Fault the page in read-only so the store is a permission fault. */
	if (segv_page[0] != 0)
		_exit(SETUP_EXIT_CODE);
#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = (void (*)(int))unprotect_and_return;
#else
	sa.sa_sigaction = unprotect_and_return;
#endif
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGSEGV, &sa, 0) != 0)
		_exit(SETUP_EXIT_CODE);
	if (!store_byte_resumed(segv_page, FAULT_STORE_VALUE) ||
	    segv_handler_runs != 1 || segv_page[0] != FAULT_STORE_VALUE)
		_exit(NOT_RESUMED_EXIT_CODE);
	_exit(RESUMED_EXIT_CODE);
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
		if (which == SIGSEGV)
			store_to_readonly_page();
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

static int exited_with(int status, int code)
{
	if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == code)
		return 1;
	ksft_print_num("wait_status", status);
	return 0;
}

static int exited_from_handler(int status)
{
	return exited_with(status, HANDLER_EXIT_CODE);
}

int main(void)
{
	ksft_set_plan(5);
	ksft_test_result(killed_by(child_status(0, SIGILL), SIGILL),
			 "illegal_insn_default_sigill");
	ksft_test_result(exited_from_handler(child_status(SIGILL, SIGILL)),
			 "illegal_insn_runs_sigill_handler");
	ksft_test_result(killed_by(child_status(0, SIGTRAP), SIGTRAP),
			 "breakpoint_default_sigtrap");
	ksft_test_result(exited_from_handler(child_status(SIGTRAP, SIGTRAP)),
			 "breakpoint_runs_sigtrap_handler");
	ksft_test_result(exited_with(child_status(SIGSEGV, SIGSEGV),
				     RESUMED_EXIT_CODE),
			 "segv_handler_returns_to_faulting_store");
	return ksft_exit();
}
