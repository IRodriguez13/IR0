/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: wait4_probe.c
 * Description: Minimal fork+wait4 workload for Linux↔IR0 ABI audit (same static ELF)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILD_EXIT_STATUS 42

static void audit_wait4(unsigned step, const char *op, long ret, int err, int status)
{
	char buf[160];
	int n;

	if (status >= 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][wait4] step=%u op=%s ret=%ld errno=%d status=0x%x\n",
			     step, op, ret, err, status);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][wait4] step=%u op=%s ret=%ld errno=%d\n",
			     step, op, ret, err);
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	pid_t pid;
	int status;
	long wret;

	pid = fork();
	if (pid < 0)
	{
		audit_wait4(0, "fork", (long)pid, errno, -1);
		return 1;
	}
	if (pid == 0)
		_exit(CHILD_EXIT_STATUS);

	audit_wait4(0, "fork", (long)pid, 0, -1);

	status = 0;
	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_wait4(1, "wait4", wret, wret < 0 ? errno : 0, status);

	if (wret != (long)pid)
		return 1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != CHILD_EXIT_STATUS)
		return 1;

	(void)write(1, "[WAIT4OK]\n", 10);
	return 0;
}
