/**
 * IR0 userspace — KTM POSIX shm_open case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_posix_shm_case.c
 * Description: shm_open/ftruncate/mmap MAP_SHARED/shm_unlink via /dev/shm.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_posix_shm(void)
{
	int fd;
	char *a;
	char *b;
	const char *name = "/dev/shm/ktmpos";

	(void)unlink(name);
	fd = open(name, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd < 0)
	{
		say("POSIX_SHM_FAIL open\n");
		return -1;
	}
	if (ftruncate(fd, 4096) != 0)
	{
		say("POSIX_SHM_FAIL ftruncate\n");
		goto fail_fd;
	}
	a = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (a == MAP_FAILED)
	{
		say("POSIX_SHM_FAIL mmap_a\n");
		goto fail_fd;
	}
	b = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b == MAP_FAILED)
	{
		say("POSIX_SHM_FAIL mmap_b\n");
		goto fail_a;
	}
	memcpy(a, "PSHMOK", 6);
	if (memcmp(b, "PSHMOK", 6) != 0)
	{
		say("POSIX_SHM_FAIL share\n");
		goto fail_b;
	}
	(void)munmap(a, 4096);
	(void)munmap(b, 4096);
	close(fd);
	if (unlink(name) != 0)
	{
		say("POSIX_SHM_FAIL unlink\n");
		return -1;
	}
	return 0;

fail_b:
	(void)munmap(b, 4096);
fail_a:
	(void)munmap(a, 4096);
fail_fd:
	close(fd);
	(void)unlink(name);
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
		say("KTM_POSIX_SHM_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_POSIX_SHM_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "posix_shm") != 0)
	{
		say("KTM_POSIX_SHM_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_posix_shm() != 0)
	{
		(void)ktm_assert_true(kfd, "posix_shm", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "posix_shm", 1);
		say("POSIX_SHM_OK\n");
		say("KTM_POSIX_SHM_OK\n");
	}
	(void)ktm_case_end(kfd, "posix_shm", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
