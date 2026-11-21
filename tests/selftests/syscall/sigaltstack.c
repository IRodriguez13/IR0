/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sigaltstack.c
 * Description: sigaltstack(2) plus SA_ONSTACK. The handler's address must
 *              fall inside the mapping installed as the alternate stack.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SIGALTSTACK_NR 132L
#else
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#define SIGALTSTACK_NR SYS_sigaltstack
#endif

#include "ir0/ksft.h"

#ifndef SA_ONSTACK
#define SA_ONSTACK 0x08000000UL
#endif

/* Linux stack_t, LP64. ss_size sits at offset 16. */
struct altstack_arg
{
	void *ss_sp;
	int ss_flags;
	int pad;
	unsigned long ss_size;
};

#define ALT_BYTES (16UL * 1024UL)

static volatile unsigned long handler_addr;

static void on_usr1(int sig)
{
	volatile char slot;

	(void)sig;
	handler_addr = (unsigned long)&slot;
}

static int handler_on_alt(void)
{
	void *map;
	struct altstack_arg ss;
	struct sigaction sa;
	unsigned long base;
	long rc;

	map = mmap(0, ALT_BYTES, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED)
	{
		ksft_print_num("mmap_errno", errno);
		return 0;
	}

	ss.ss_sp = map;
	ss.ss_flags = 0;
	ss.pad = 0;
	ss.ss_size = ALT_BYTES;
	rc = syscall(SIGALTSTACK_NR, (long)&ss, 0L);
	if (rc != 0)
	{
		ksft_print_num("sigaltstack_errno", errno);
		return 0;
	}

#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = on_usr1;
	sa.sa_flags = SA_ONSTACK;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	__builtin_memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;
	sa.sa_flags = SA_ONSTACK;
#endif
	if (sigaction(SIGUSR1, &sa, 0) != 0)
	{
		ksft_print_num("sigaction_errno", errno);
		return 0;
	}

	handler_addr = 0;
	if (kill(getpid(), SIGUSR1) != 0)
	{
		ksft_print_num("kill_errno", errno);
		return 0;
	}

	base = (unsigned long)map;
	if (handler_addr < base || handler_addr >= base + ALT_BYTES)
	{
		ksft_print_num("handler_addr", (long)handler_addr);
		return 0;
	}
	return 1;
}

int main(void)
{
	ksft_set_plan(1);
	ksft_test_result(handler_on_alt(), "sigaltstack_on_alt");
	return ksft_exit();
}
