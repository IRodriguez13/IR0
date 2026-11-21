/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: shm_mode.c
 * Description: IPC_STAT reports the permission bits passed to shmget.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#define SYS_shmget 194
#define SYS_shmctl 195
#else
#include <sys/syscall.h>
#endif

#include "ir0/ksft.h"

#define SHM_CREAT 01000
#define SHM_STAT 2
#define SHM_MODE 0640

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
	int shm_uid;
	int shm_gid;
};

int main(void)
{
	struct shm_info st;
	volatile unsigned char *zero;
	unsigned int i;
	long id;
	long rc;
	int pass;

	ksft_set_plan(1);
	zero = (volatile unsigned char *)&st;
	for (i = 0; i < sizeof(st); i++)
		zero[i] = 0;

	id = syscall(SYS_shmget, 0, 4096, SHM_CREAT | SHM_MODE);
	if (id < 0)
	{
		ksft_test_result(0, "shm_mode");
		return ksft_exit();
	}
	rc = syscall(SYS_shmctl, id, SHM_STAT, (long)&st);
	pass = rc == 0 && st.shm_perm_mode == SHM_MODE;
	ksft_test_result(pass, "shm_mode");
	return ksft_exit();
}
