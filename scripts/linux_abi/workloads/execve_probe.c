/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: execve_probe.c
 * Description: fork+execve workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef EXEC_HELPER_PATH
#define EXEC_HELPER_PATH "/sbin/exec_helper"
#endif

#define EXEC_NOENT_PATH "/tmp/ir0exec_noent"

static void audit_execve(unsigned step, const char *op, long ret, int err,
			 int status)
{
	char buf[256];
	int n;

	if (status >= 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][execve] step=%u op=%s ret=%ld errno=%d status=0x%x\n",
			     step, op, ret, err, status);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][execve] step=%u op=%s ret=%ld errno=%d\n",
			     step, op, ret, err);
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	pid_t pid;
	int st;
	char *argv[] = { (char *)EXEC_HELPER_PATH, NULL };
	char *envp[] = { NULL };

	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0)
	{
		(void)syscall(SYS_execve, EXEC_HELPER_PATH, argv, envp);
		audit_execve(0, "execve_ok", -1, errno, -1);
		_exit(127);
	}

	st = 0;
	if (waitpid(pid, &st, 0) < 0)
		return 1;
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		return 1;
	audit_execve(0, "execve_ok", (long)pid, 0, st);

	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0)
	{
		(void)syscall(SYS_execve, EXEC_NOENT_PATH, argv, envp);
		audit_execve(1, "execve_noent", -1, errno, -1);
		_exit(127);
	}

	st = 0;
	(void)waitpid(pid, &st, 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st) == 0)
		return 1;

	(void)write(1, "[EXECVEOK]\n", 11);
	return 0;
}
