/**
 * IR0 userspace — KTM userdev pilot
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_chroot_case.c
 * Description: chroot(2) root remap and fork inheritance, reported via KTM.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int expect(int fd, const char *name, int cond, int *fails)
{
	(void)ktm_assert_true(fd, name, cond);
	if (!cond)
		(*fails)++;
	return cond;
}

static int write_file(const char *path, const char *data)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ssize_t n;
	size_t len;

	if (fd < 0)
		return -1;
	len = strlen(data);
	n = write(fd, data, len);
	close(fd);
	return (n == (ssize_t)len) ? 0 : -1;
}

static int file_starts_with(const char *path, const char *expect)
{
	char buf[64];
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';
	return strncmp(buf, expect, strlen(expect)) == 0;
}

int main(void)
{
	int fd;
	int fails = 0;
	char cwd[256];
	pid_t pid;
	int status = 0;

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_FAIL open\n");
		return 1;
	}

	if (ktm_case_begin(fd, "chroot_jail") != 0)
	{
		say("KTM_USERDEV_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (mkdir("/jail", 0755) != 0 && errno != EEXIST)
	{
		expect(fd, "mkdir_jail", 0, &fails);
		goto done;
	}
	expect(fd, "mkdir_jail", 1, &fails);
	expect(fd, "write_inside",
	       write_file("/jail/inside.txt", "inside\n") == 0, &fails);
	expect(fd, "write_outside",
	       write_file("/outside.txt", "outside\n") == 0, &fails);

	if (chroot("/jail") != 0)
	{
		expect(fd, "chroot_call", 0, &fails);
		goto done;
	}
	expect(fd, "chroot_call", 1, &fails);

	expect(fd, "open_inside",
	       file_starts_with("/inside.txt", "inside"), &fails);

	{
		int outside = open("/outside.txt", O_RDONLY);

		if (outside >= 0)
			close(outside);
		expect(fd, "outside_denied", outside < 0, &fails);
	}

	expect(fd, "chdir_root", chdir("/") == 0, &fails);
	expect(fd, "getcwd_root",
	       getcwd(cwd, sizeof(cwd)) != NULL && strcmp(cwd, "/") == 0, &fails);

	pid = fork();
	if (pid < 0)
	{
		expect(fd, "fork_inherit", 0, &fails);
	}
	else if (pid == 0)
	{
		int outside;

		if (!file_starts_with("/inside.txt", "inside"))
			_exit(11);
		outside = open("/outside.txt", O_RDONLY);
		if (outside >= 0)
		{
			close(outside);
			_exit(12);
		}
		_exit(0);
	}
	else
	{
		if (waitpid(pid, &status, 0) != pid)
			expect(fd, "fork_inherit", 0, &fails);
		else
			expect(fd, "fork_inherit",
			       WIFEXITED(status) && WEXITSTATUS(status) == 0,
			       &fails);
	}

done:
	/* The KTM fd was opened before chroot(), so reporting still works. */
	(void)ktm_case_end(fd, "chroot_jail", fails == 0 ? 0 : 1);
	ktm_close(fd);

	if (fails == 0)
	{
		say("KTM_USERDEV_OK\n");
		return 0;
	}
	say("KTM_USERDEV_FAIL\n");
	return 1;
}
