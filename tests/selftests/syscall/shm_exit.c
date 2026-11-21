/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: shm_exit.c
 * Description: A child's shmat is dropped on exit. IPC_RMID then destroys
 *              the segment when the parent detaches.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SYS_shmget 194
#define SYS_shmctl 195
#define SYS_shmat 196
#define SYS_shmdt 197
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* Matches includes/ir0/sysv_shm.h. Fork does not copy attaches. */
#define SHM_CREAT 01000
#define SHM_RMID 0
#define SHM_STAT 2

struct shm_info
{
	size_t shm_segsz;
	int shm_nattch;
	int shm_perm_mode;
	int shm_cpid;
	int shm_lpid;
	long shm_atime;
	long shm_dtime;
	long shm_ctime;
};

static int shm_inval(void)
{
#ifdef IR0_SELFTEST_NOLIBC
	return errno == 22;
#else
	return errno == EINVAL;
#endif
}

static int shm_nattch(long id, int *out)
{
	struct shm_info st;
	long rc;

	rc = syscall(SYS_shmctl, id, SHM_STAT, (long)&st);
	if (rc < 0)
		return -1;
	*out = st.shm_nattch;
	return 0;
}

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
	int own;
	int after;
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
	page[0] = 0;
	if (shm_nattch(id, &own) < 0 || own < 1)
		goto out;

	pid = fork();
	if (pid < 0)
		goto out;
	if (pid == 0)
	{
		long child;

		child = syscall(SYS_shmat, id, 0, 0);
		if (child < 0)
			_exit(2);
		*(volatile char *)child = 0x5a;
		_exit(0);
	}

	if (wait_child(pid, &status) < 0)
		goto out;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		goto out;
	if (page[0] != 0x5a)
		goto out;
	if (shm_nattch(id, &after) < 0 || after != own)
		goto out;
	if (syscall(SYS_shmctl, id, SHM_RMID, 0) < 0)
		goto out;
	if (syscall(SYS_shmdt, addr) < 0)
		goto out;

	errno = 0;
	if (syscall(SYS_shmctl, id, SHM_STAT, 0) < 0 && shm_inval())
		pass = 1;
out:
	ksft_test_result(pass, "shm_exit");
	return ksft_exit();
}
