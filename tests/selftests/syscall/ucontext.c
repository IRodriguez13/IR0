/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ucontext.c
 * Description: A SA_SIGINFO handler edits the Linux ucontext and
 *              rt_sigreturn resumes at that instruction pointer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <signal.h>
#include <string.h>
#include <unistd.h>
#endif

/*
 * Byte offset of the resumed instruction pointer inside the ucontext
 * the handler receives. musl x86-64 gregs[REG_RIP]; musl aarch64
 * mcontext.pc.
 */
#if defined(__aarch64__)
#define UCONTEXT_IP 440
#else
#define UCONTEXT_IP 168
#endif

static void land(void)
{
	ksft_test_result(1, "ucontext_sets_ip");
	_exit(ksft_exit());
}

static void on_usr1(int sig, void *info, void *uc)
{
	unsigned char *bytes = uc;

	(void)sig;
	(void)info;
	*(unsigned long *)(bytes + UCONTEXT_IP) = (unsigned long)land;
}

int main(void)
{
	struct sigaction sa;

	ksft_set_plan(1);
#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = (void (*)(int))on_usr1;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = (void (*)(int, siginfo_t *, void *))on_usr1;
	sa.sa_flags = SA_SIGINFO;
#endif
	if (sigaction(SIGUSR1, &sa, 0) != 0)
	{
		ksft_test_result(0, "ucontext_sets_ip");
		return ksft_exit();
	}
	kill(getpid(), SIGUSR1);
	ksft_test_result(0, "ucontext_sets_ip");
	return ksft_exit();
}
