/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kill_sigterm_probe.c
 * Description: Isolated fork/pause/kill(SIGTERM)/wait4 probe for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define EXPECT_STATUS 0x000f

static void audit_ks(unsigned step, const char *op, long ret, int err, int status)
{
	char buf[192];
	int n;

	if (status >= 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][kill_sigterm] step=%u op=%s ret=%ld errno=%d status=0x%x\n",
			     step, op, ret, err, status);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][kill_sigterm] step=%u op=%s ret=%ld errno=%d\n",
			     step, op, ret, err);
	}
	if (n > 0)
	{
		(void)write(1, buf, (size_t)n);
		(void)fsync(1);
	}
}

int main(void)
{
	pid_t pid;
	int status = 0;
	long wret;
	int kret;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	pid = fork();
	if (pid < 0)
	{
		audit_ks(1, "fork", (long)pid, errno, -1);
		return 1;
	}
	if (pid == 0)
	{
		for (;;)
			pause();
	}

	audit_ks(1, "fork", (long)pid, 0, -1);

	kret = kill(pid, SIGTERM);
	audit_ks(2, "kill", (long)kret, kret != 0 ? errno : 0, -1);
	if (kret != 0)
		return 1;

	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_ks(3, "kill_sigterm", wret, wret < 0 ? errno : 0, status);

	if (wret != (long)pid)
		return 1;
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
		return 1;
	if (status != EXPECT_STATUS)
		return 1;

	(void)write(1, "[KILLSIGTERMOK]\n", 16);
	(void)fsync(1);
	return 0;
}
