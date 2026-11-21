/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: shm_rdonly.c
 * Description: shmat(SHM_RDONLY) is read-only: a store raises SIGSEGV.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SYS_shmget 194
#define SYS_shmat 196
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Matches includes/ir0/sysv_shm.h IR0_IPC_CREAT and IR0_SHM_RDONLY. */
#define SHM_CREAT 01000
#define SHM_RDONLY 010000

static volatile int saw_segv;

static void on_segv(int sig)
{
	(void)sig;
	saw_segv = 1;
	ksft_test_result(saw_segv, "shm_rdonly");
	_exit(ksft_exit());
}

int main(void)
{
	long id;
	long addr;
	struct sigaction sa;
	volatile char *page;

	ksft_set_plan(1);
	id = syscall(SYS_shmget, 0, 4096, SHM_CREAT);
	if (id < 0)
		goto out;
	addr = syscall(SYS_shmat, id, 0, SHM_RDONLY);
	if (addr < 0)
		goto out;

#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = on_segv;
	sa.sa_flags = 0;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_segv;
#endif
	if (sigaction(SIGSEGV, &sa, 0) != 0)
		goto out;

	page = (volatile char *)addr;
	page[0] = 1;
out:
	ksft_test_result(saw_segv, "shm_rdonly");
	return ksft_exit();
}
