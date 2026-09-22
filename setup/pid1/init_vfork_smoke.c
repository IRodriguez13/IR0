/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init_vfork_smoke.c
 * Description: Linux vfork(2) parent-suspend and shared-mm probe (PID1).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static void tag(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static int wait_exit(pid_t pid)
{
	int status = 0;

	if (waitpid(pid, &status, 0) != pid)
		return -1;
	if (!WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}

int main(void)
{
	volatile int *cell;
	pid_t pid;
	int rc;

	cell = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (cell == MAP_FAILED)
	{
		tag("VFORK_MMAP_FAIL\n");
		return 1;
	}

	*cell = 0;
	pid = vfork();
	if (pid < 0)
	{
		tag("VFORK_SHARE_SYSCALL_FAIL\n");
		return 1;
	}
	if (pid == 0)
	{
		*cell = 0x5f00;
		_exit(0);
	}
	if (*cell != 0x5f00)
	{
		tag("VFORK_SHARE_FAIL\n");
		return 1;
	}
	if (wait_exit(pid) != 0)
	{
		tag("VFORK_SHARE_WAIT_FAIL\n");
		return 1;
	}
	tag("VFORK_SHARE_OK\n");

	*cell = 0;
	pid = vfork();
	if (pid < 0)
	{
		tag("VFORK_EXEC_SYSCALL_FAIL\n");
		return 1;
	}
	if (pid == 0)
	{
		char *true_argv[] = { "true", NULL };
		char *bb_argv[] = { "busybox", "true", NULL };

		*cell = 0xe01;
		execve("/bin/true", true_argv, NULL);
		execve("/bin/busybox", bb_argv, NULL);
		_exit(127);
	}
	if (*cell != 0xe01)
	{
		tag("VFORK_EXEC_PARENT_EARLY\n");
		return 1;
	}
	rc = wait_exit(pid);
	if (rc != 0)
	{
		tag("VFORK_EXEC_FAIL\n");
		return 1;
	}
	tag("VFORK_EXEC_OK\n");
	tag("VFORK_ALL_OK\n");
	return 0;
}
