/**
 * IR0 userspace — KTM memfd MAP_SHARED case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_memfd_shared_case.c
 * Description: memfd_create + ftruncate + dual MAP_SHARED mmap.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "libktm_user.h"

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 1u
#endif

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_memfd_shared(void)
{
	int fd;
	char *a;
	char *b;

	fd = (int)syscall(SYS_memfd_create, "ktm-memfd", MFD_CLOEXEC);
	if (fd < 0)
		return -1;
	if (ftruncate(fd, 4096) != 0)
		goto fail_fd;
	a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (a == MAP_FAILED)
		goto fail_fd;
	b = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b == MAP_FAILED)
		goto fail_a;
	memcpy(a, "MFDOK", 5);
	if (memcmp(b, "MFDOK", 5) != 0)
		goto fail_b;
	if (munmap(a, 4096) != 0)
		goto fail_b;
	if (munmap(b, 4096) != 0)
		goto fail_fd;
	close(fd);
	return 0;

fail_b:
	(void)munmap(b, 4096);
fail_a:
	(void)munmap(a, 4096);
fail_fd:
	close(fd);
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
		say("KTM_MEMFD_SHARED_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_MEMFD_SHARED_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "memfd_shared") != 0)
	{
		say("KTM_MEMFD_SHARED_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_memfd_shared() != 0)
	{
		(void)ktm_assert_true(kfd, "memfd_shared", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "memfd_shared", 1);
		say("MEMFD_SHARED_OK\n");
		say("KTM_MEMFD_SHARED_OK\n");
	}
	(void)ktm_case_end(kfd, "memfd_shared", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
