/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: segv_accerr.c
 * Description: A write to a present read-only page is SEGV_ACCERR.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

static int accerr_code(const siginfo_t *info)
{
	if (!info)
		return 0;
#ifdef IR0_SELFTEST_NOLIBC
	return info->si_code == 2;
#else
	return info->si_code == SEGV_ACCERR;
#endif
}

static void on_segv(int sig, siginfo_t *info, void *uc)
{
	(void)sig;
	(void)uc;
	ksft_test_result(accerr_code(info), "segv_accerr");
	_exit(ksft_exit());
}

int main(void)
{
	struct sigaction sa;
	volatile char *page;

	ksft_set_plan(1);
	page = mmap(0, 4096, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (page == MAP_FAILED)
	{
		ksft_test_result(0, "segv_accerr");
		return ksft_exit();
	}
	page[0] = 1;
	if (mprotect((void *)page, 4096, PROT_READ) != 0)
	{
		ksft_test_result(0, "segv_accerr");
		return ksft_exit();
	}

#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = (void (*)(int))on_segv;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_segv;
	sa.sa_flags = SA_SIGINFO;
#endif
	if (sigaction(SIGSEGV, &sa, 0) != 0)
	{
		ksft_test_result(0, "segv_accerr");
		return ksft_exit();
	}
	page[0] = 2;
	ksft_test_result(0, "segv_accerr");
	return ksft_exit();
}
