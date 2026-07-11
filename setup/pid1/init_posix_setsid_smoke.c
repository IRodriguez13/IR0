/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_posix_setsid_smoke.c
 * Description: Child setsid + setpgid(0,0) job-control MVP.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

int main(void)
{
	pid_t child;
	int status = 0;
	pid_t sid;
	int rc;

	child = fork();
	if (child < 0)
	{
		tag("POSIX_SETSID_FORK_FAIL\n");
		return 1;
	}

	if (child == 0)
	{
		sid = setsid();
		if (sid < 0)
		{
			tag("SETSID_FAIL\n");
			_exit(1);
		}
		tag("SETSID_OK\n");
		rc = setpgid(0, 0);
		if (rc != 0)
		{
			tag("SETPGID_FAIL\n");
			_exit(2);
		}
		tag("SETPGID_OK\n");
		_exit(0);
	}

	if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
	{
		tag("POSIX_SETSID_WAIT_FAIL\n");
		return 1;
	}

	tag("POSIX_SETSID_OK\n");
	return 0;
}
