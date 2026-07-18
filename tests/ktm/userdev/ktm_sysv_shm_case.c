/**
 * IR0 userspace — KTM SysV shm case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_sysv_shm_case.c
 * Description: shmget/shmat/shmdt/shmctl MIT-SHM prep gate.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_sysv_shm(void)
{
	int id;
	char *a;
	char *b;

	id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
	if (id < 0)
		return -1;
	a = shmat(id, NULL, 0);
	if (a == (void *)-1)
		goto fail_id;
	b = shmat(id, NULL, 0);
	if (b == (void *)-1)
		goto fail_a;
	memcpy(a, "SHMOK", 5);
	if (memcmp(b, "SHMOK", 5) != 0)
		goto fail_b;
	if (shmdt(a) != 0)
		goto fail_b;
	if (shmdt(b) != 0)
		goto fail_id;
	if (shmctl(id, IPC_RMID, NULL) != 0)
		return -1;
	return 0;

fail_b:
	(void)shmdt(b);
fail_a:
	(void)shmdt(a);
fail_id:
	(void)shmctl(id, IPC_RMID, NULL);
	return -1;
}

int main(void)
{
	int kfd;
	int fails = 0;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_SYSV_SHM_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_SYSV_SHM_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "sysv_shm") != 0)
	{
		say("KTM_SYSV_SHM_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_sysv_shm() != 0)
	{
		(void)ktm_assert_true(kfd, "sysv_shm", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "sysv_shm", 1);
		say("SYSV_SHM_OK\n");
		say("KTM_SYSV_SHM_OK\n");
	}
	(void)ktm_case_end(kfd, "sysv_shm", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
