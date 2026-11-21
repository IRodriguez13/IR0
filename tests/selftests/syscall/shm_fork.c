/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: shm_fork.c
 * Description: A store through the child's inherited shmat is visible
 *              to the parent. SysV shm stays shared across fork.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SYS_shmget 194
#define SYS_shmat 196
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Matches includes/ir0/sysv_shm.h IR0_IPC_CREAT. */
#define SHM_CREAT 01000

static int wait_child(int pid, int *status)
{
#ifdef IR0_SELFTEST_NOLIBC
	return wait4(pid, status, 0, 0);
#else
	return waitpid(pid, status, 0);
#endif
}

int main(void)
{
	long id;
	long addr;
	int pid;
	int status;
	int pass;
	volatile char *page;

	ksft_set_plan(1);
	pass = 0;

	id = syscall(SYS_shmget, 0, 4096, SHM_CREAT);
	if (id < 0)
		goto out;
	addr = syscall(SYS_shmat, id, 0, 0);
	if (addr < 0)
		goto out;
	page = (volatile char *)addr;
	page[0] = 0x11;

	pid = fork();
	if (pid < 0)
		goto out;
	if (pid == 0)
	{
		page[0] = 0x5a;
		_exit(0);
	}

	if (wait_child(pid, &status) < 0)
		goto out;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		goto out;
	if (page[0] == 0x5a)
		pass = 1;
out:
	ksft_test_result(pass, "shm_fork");
	return ksft_exit();
}
