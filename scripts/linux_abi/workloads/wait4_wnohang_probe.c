/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: wait4_wnohang_probe.c
 * Description: Isolated wait4(WNOHANG) + blocking reap probe for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned g_step;

static long audit_wait_ret(long wret, int *err)
{
	if (wret < 0 && *err == 0 && wret > -4096)
		*err = (int)-wret;
	if (wret == -ECHILD)
	{
		*err = ECHILD;
		return -1;
	}
	if (wret < 0 && *err != 0)
		return -1;
	return wret;
}

static void audit_wn(unsigned step, const char *op, long ret, int err, int status)
{
	char buf[256];
	int n;

	g_step = step;
	if (status >= 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][wait4_wnohang] step=%u op=%s ret=%ld errno=%d status=0x%x\n",
			     step, op, ret, err, status);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][wait4_wnohang] step=%u op=%s ret=%ld errno=%d\n",
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
	int readyfd[2];
	int releasefd[2];
	char ch;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (pipe(readyfd) != 0 || pipe(releasefd) != 0)
		return 1;

	pid = fork();
	if (pid == 0)
	{
		volatile char stack_cow = 0;

		(void)stack_cow;
		(void)write(readyfd[1], "R", 1);
		(void)close(readyfd[0]);
		(void)close(readyfd[1]);
		(void)close(releasefd[1]);
		if (read(releasefd[0], &ch, 1) != 1)
			_exit(1);
		(void)close(releasefd[0]);
		_exit(5);
	}
	if (pid < 0)
	{
		audit_wn(1, "fork", (long)pid, errno, -1);
		return 1;
	}

	{
		volatile char stack_cow = 1;

		(void)stack_cow;
	}

	audit_wn(1, "fork", (long)pid, 0, -1);

	(void)close(readyfd[1]);
	(void)close(releasefd[0]);
	if (read(readyfd[0], &ch, 1) != 1)
		return 1;
	(void)close(readyfd[0]);

	wret = (long)syscall(SYS_wait4, (long)pid, &status, WNOHANG, NULL);
	audit_wn(2, "wait4_wnohang_alive", wret, wret < 0 ? errno : 0, status);
	if (wret != 0)
		return 1;

	ch = 'G';
	if (write(releasefd[1], &ch, 1) != 1)
		return 1;
	(void)close(releasefd[1]);

	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_wn(3, "wait4_block_reap", wret, wret < 0 ? errno : 0, status);
	if (wret != (long)pid)
		return 1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 5)
		return 1;

	wret = (long)syscall(SYS_wait4, (long)pid, &status, WNOHANG, NULL);
	{
		int err = wret < 0 ? errno : 0;

		wret = audit_wait_ret(wret, &err);
		audit_wn(4, "wait4_wnohang_echild", wret, err, status);
		if (wret != -1 || err != ECHILD)
			return 1;
	}

	{
		const char ok[] = "[WAIT4WNOHANGOK]\n";
		(void)write(1, ok, sizeof(ok) - 1);
	}
	return 0;
}
