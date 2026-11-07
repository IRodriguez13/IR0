/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fault.c
 * Description: User page-fault selftest: anonymous demand paging and
 *              SIGSEGV on an unmapped access, through the common MM handler.
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

#define FAULT_PAGE_SIZE 4096UL
#define FAULT_UNMAPPED_ADDR 0x8UL

/* Two untouched anonymous pages must fault in and keep what was written. */
static int lazy_anon_rw(void)
{
	volatile unsigned char *p;
	unsigned long i;

	p = mmap(0, 2 * FAULT_PAGE_SIZE, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
	{
		ksft_print_num("mmap_errno", errno);
		return 0;
	}

	for (i = 0; i < 2; i++)
		p[i * FAULT_PAGE_SIZE + 7] = (unsigned char)(0xa5 + i);
	for (i = 0; i < 2; i++)
		if (p[i * FAULT_PAGE_SIZE + 7] != (unsigned char)(0xa5 + i))
		{
			ksft_print_num("readback_page", (long)i);
			return 0;
		}
	return 1;
}

/*
 * Precondition for segv_child_killed: a fork child resumes with its own user
 * registers and runs its post-fork code (corrupt registers would crash it).
 */
static int fork_child_exit_status(void)
{
	int status = 0;
	int pid;

	pid = fork();
	if (pid < 0)
	{
		ksft_print_num("fork_errno", errno);
		return 0;
	}
	if (pid == 0)
		_exit(42);
	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return 0;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
	{
		ksft_print_num("wait_status", status);
		return 0;
	}
	return 1;
}

/* A store to an unmapped address must kill the task with SIGSEGV. */
static int segv_child_killed(void)
{
	int status = 0;
	int pid;

	pid = fork();
	if (pid < 0)
	{
		ksft_print_num("fork_errno", errno);
		return 0;
	}
	if (pid == 0)
	{
		*(volatile unsigned long *)FAULT_UNMAPPED_ADDR = 1;
		_exit(0);
	}
	if (wait4(pid, &status, 0, 0) != pid)
	{
		ksft_print_num("wait4_errno", errno);
		return 0;
	}
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV)
	{
		ksft_print_num("wait_status", status);
		return 0;
	}
	return 1;
}

int main(void)
{
	ksft_set_plan(3);
	ksft_test_result(lazy_anon_rw(), "lazy_anon_rw");
	ksft_test_result(fork_child_exit_status(), "fork_child_exit_status");
	ksft_test_result(segv_child_killed(), "segv_child_killed");
	return ksft_exit();
}
