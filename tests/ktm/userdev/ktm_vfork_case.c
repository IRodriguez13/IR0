/**
 * IR0 userspace — KTM userdev pilot
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_vfork_case.c
 * Description: vfork(2) shared-mm and parent-suspend probe, reported via KTM.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define VFORK_CELL_SHARE 0x5f00
#define VFORK_CELL_EXEC  0xe01

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
	int fd;
	int fails = 0;
	volatile int *cell;
	pid_t pid;

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_FAIL open\n");
		return 1;
	}

	if (ktm_case_begin(fd, "vfork_share") != 0)
	{
		say("KTM_USERDEV_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	cell = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (cell == MAP_FAILED)
	{
		expect(fd, "mmap_ok", 0, &fails);
		goto done;
	}
	expect(fd, "mmap_ok", 1, &fails);

	/* vfork child writes into the shared address space, then exits. */
	*cell = 0;
	pid = vfork();
	if (pid < 0)
	{
		expect(fd, "vfork_share_ok", 0, &fails);
	}
	else if (pid == 0)
	{
		*cell = VFORK_CELL_SHARE;
		_exit(0);
	}
	else
	{
		expect(fd, "vfork_share_ok", *cell == VFORK_CELL_SHARE, &fails);
		expect(fd, "vfork_share_child_exit", wait_exit(pid) == 0, &fails);
	}

	/* vfork child execs: the parent must stay suspended until execve. */
	*cell = 0;
	pid = vfork();
	if (pid < 0)
	{
		expect(fd, "vfork_exec_ok", 0, &fails);
	}
	else if (pid == 0)
	{
		char *argv[] = { "f41true", NULL };

		*cell = VFORK_CELL_EXEC;
		execve("/bin/f41true", argv, NULL);
		_exit(127);
	}
	else
	{
		expect(fd, "vfork_exec_parent_suspended",
		       *cell == VFORK_CELL_EXEC, &fails);
		expect(fd, "vfork_exec_child_exit", wait_exit(pid) == 0, &fails);
	}

done:
	(void)ktm_case_end(fd, "vfork_share", fails == 0 ? 0 : 1);
	ktm_close(fd);

	if (fails == 0)
	{
		say("KTM_USERDEV_OK\n");
		return 0;
	}
	say("KTM_USERDEV_FAIL\n");
	return 1;
}
