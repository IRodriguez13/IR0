/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: shm_times.c
 * Description: IPC_STAT reports create and attach times.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SYS_shmget 194
#define SYS_shmctl 195
#define SYS_shmat 196
#else
#include <sys/syscall.h>
#endif

#include "ir0/ksft.h"

#define SHM_CREAT 01000
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

int main(void)
{
	struct shm_info st;
	volatile unsigned char *zero;
	unsigned int i;
	long id;
	long addr;
	long rc;
	int pass;

	ksft_set_plan(1);
	zero = (volatile unsigned char *)&st;
	for (i = 0; i < sizeof(st); i++)
		zero[i] = 0;

	id = syscall(SYS_shmget, 0, 4096, SHM_CREAT);
	if (id < 0)
	{
		ksft_test_result(0, "shm_times");
		return ksft_exit();
	}
	addr = syscall(SYS_shmat, id, 0, 0);
	if (addr < 0)
	{
		ksft_test_result(0, "shm_times");
		return ksft_exit();
	}
	rc = syscall(SYS_shmctl, id, SHM_STAT, (long)&st);
	pass = rc == 0 && st.shm_ctime > 0 && st.shm_atime > 0 &&
	       st.shm_dtime == 0;
	ksft_test_result(pass, "shm_times");
	return ksft_exit();
}
