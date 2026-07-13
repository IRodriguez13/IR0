/**
 * IR0 userspace — KTM posix/pseudofs case (FASE53B analogue)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_posix_pseudofs_case.c
 * Description: faccessat/chdir/access + /dev node opens + getdents cursor on /dev.
 *              Optional virtio-9p host share report.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int check_dev_nodes(void)
{
	struct stat st;
	int fd;

	if (stat("/dev/null", &st) != 0)
		return -1;
	if (stat("/dev/zero", &st) != 0)
		return -1;
	fd = open("/dev/null", O_RDWR);
	if (fd < 0)
		return -1;
	close(fd);
	fd = open("/dev/zero", O_RDONLY);
	if (fd < 0)
		return -1;
	close(fd);
	return 0;
}

static int check_access_paths(void)
{
	struct stat st;

	if (access("/dev/null", R_OK | W_OK) != 0)
		return -1;
	if (faccessat(AT_FDCWD, "/dev/zero", R_OK, 0) != 0)
		return -1;
	/* Bad flags must fail (do not touch errno: -Iincludes shadows musl). */
	if (faccessat(AT_FDCWD, "/dev/zero", R_OK, 0x40000000) == 0)
		return -1;
	if (chdir("/tmp") != 0)
		return -1;
	if (access(".", R_OK | W_OK | X_OK) != 0)
		return -1;
	if (access("/proc", F_OK) == 0 && stat("/proc", &st) != 0)
		return -1;
	return 0;
}

static int check_getdents_dev(void)
{
	char buf[2048];
	long nread;
	int fd;
	int saw_entry;

	fd = open("/dev", O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return -1;

	saw_entry = 0;
	for (;;)
	{
		nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
		if (nread < 0)
		{
			close(fd);
			return -1;
		}
		if (nread == 0)
			break;
		saw_entry = 1;
	}
	if (!saw_entry)
	{
		close(fd);
		return -1;
	}

	nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
	close(fd);
	if (nread != 0)
		return -1;

	fd = open("/dev", O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return -1;
	nread = syscall(SYS_getdents64, fd, buf, sizeof(buf));
	close(fd);
	if (nread <= 0)
		return -1;

	say("KTM_GETDENTS_DEV_CURSOR_OK\n");
	return 0;
}

static void try_hostshare_report(int ok)
{
	const char *payload;
	int fd;
	ssize_t n;

	(void)mkdir("/mnt", 0755);
	(void)mkdir("/mnt/host", 0755);
	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
	{
		say("KTM_HOSTSHARE_SKIP\n");
		return;
	}
	say("KTM_HOSTSHARE_MOUNT_OK\n");
	payload = ok ? "KTM_USERDEV_POSIX_PSEUDOFS_OK\n"
		     : "KTM_USERDEV_POSIX_PSEUDOFS_FAIL\n";
	fd = open("/mnt/host/ktm_posix_pseudofs.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		say("KTM_HOSTSHARE_WRITE_SKIP\n");
		return;
	}
	n = write(fd, payload, strlen(payload));
	(void)close(fd);
	if (n == (ssize_t)strlen(payload))
		say("KTM_HOSTSHARE_REPORT_OK\n");
	else
		say("KTM_HOSTSHARE_WRITE_SKIP\n");
}

int main(void)
{
	int kfd;
	int fails = 0;
	int nodes_ok;
	int acc_ok;
	int gd_ok;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_USERDEV_POSIX_PSEUDOFS_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_POSIX_PSEUDOFS_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "posix_pseudofs") != 0)
	{
		say("KTM_USERDEV_POSIX_PSEUDOFS_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_checkpoint(kfd, "dev_nodes");
	nodes_ok = (check_dev_nodes() == 0);
	if (!nodes_ok)
		fails++;
	if (ktm_assert_true(kfd, "dev_nodes", nodes_ok) != 0)
		fails++;

	(void)ktm_checkpoint(kfd, "access_begin");
	acc_ok = (check_access_paths() == 0);
	if (!acc_ok)
		fails++;
	if (ktm_assert_true(kfd, "access_paths", acc_ok) != 0)
		fails++;

	(void)ktm_checkpoint(kfd, "getdents_dev");
	gd_ok = (check_getdents_dev() == 0);
	if (!gd_ok)
		fails++;
	if (ktm_assert_true(kfd, "getdents_dev", gd_ok) != 0)
		fails++;

	(void)ktm_case_end(kfd, "posix_pseudofs", fails == 0 ? 0 : 1);
	ktm_close(kfd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("KTM_USERDEV_POSIX_PSEUDOFS_OK\n");
		for (;;)
			(void)pause();
		return 0;
	}
	say("KTM_USERDEV_POSIX_PSEUDOFS_FAIL\n");
	for (;;)
		(void)pause();
	return 1;
}
