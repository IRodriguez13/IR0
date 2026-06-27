/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_lifecycle_probe.c
 * Description: Process lifecycle capability bundle (fork/exec/wait/signals/reparent)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef EXEC_HELPER_PATH
#define EXEC_HELPER_PATH "/sbin/exec_helper"
#endif

#ifndef EXEC_NOENT_PATH
#define EXEC_NOENT_PATH "/tmp/ir0exec_noent"
#endif

static unsigned g_step;

static void audit_pl(const char *op, long ret, int err, int status, const char *flags)
{
	char buf[256];
	int n;

	g_step++;
	if (status >= 0 && flags)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][process_lifecycle] step=%u op=%s ret=%ld errno=%d status=0x%x flags=%s\n",
			     g_step, op, ret, err, status, flags);
	}
	else if (status >= 0)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][process_lifecycle] step=%u op=%s ret=%ld errno=%d status=0x%x\n",
			     g_step, op, ret, err, status);
	}
	else if (flags)
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][process_lifecycle] step=%u op=%s ret=%ld errno=%d flags=%s\n",
			     g_step, op, ret, err, flags);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][process_lifecycle] step=%u op=%s ret=%ld errno=%d\n",
			     g_step, op, ret, err);
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

static int step_wait4_exit42(void)
{
	pid_t pid;
	int status = 0;
	long wret;

	pid = fork();
	if (pid < 0)
	{
		audit_pl("wait4_exit42", (long)pid, errno, -1, NULL);
		return -1;
	}
	if (pid == 0)
		_exit(42);

	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_pl("wait4_exit42", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != (long)pid)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
		return -1;
	return 0;
}

static int step_wait4_any(void)
{
	pid_t pid;
	int status = 0;
	long wret;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
		_exit(10);

	wret = (long)syscall(SYS_wait4, (long)-1, &status, 0, NULL);
	audit_pl("wait4_any", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != (long)pid)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 10)
		return -1;
	return 0;
}

static int step_wait4_wnohang(void)
{
	pid_t pid;
	int status = 0;
	long wret;
	int pipefd[2];
	char ch;
	struct timespec ts = { 2, 0 };

	if (pipe(pipefd) != 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		(void)write(pipefd[1], "R", 1);
		(void)close(pipefd[0]);
		(void)close(pipefd[1]);
		(void)nanosleep(&ts, NULL);
		_exit(5);
	}

	(void)close(pipefd[1]);
	if (read(pipefd[0], &ch, 1) != 1)
		return -1;
	(void)close(pipefd[0]);

	wret = (long)syscall(SYS_wait4, (long)pid, &status, WNOHANG, NULL);
	audit_pl("wait4_wnohang_alive", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != 0)
		return -1;

	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_pl("wait4_wnohang_reap", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != (long)pid)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 5)
		return -1;
	return 0;
}

static int step_wait4_echild(void)
{
	int status = 0;
	long wret;

	wret = (long)syscall(SYS_wait4, (long)-1, &status, 0, NULL);
	audit_pl("wait4_echild", wret, errno, status, NULL);
	if (wret != -1 || errno != ECHILD)
		return -1;
	return 0;
}

static int step_execve_ok(void)
{
	pid_t pid;
	int status = 0;
	char *argv[] = { (char *)EXEC_HELPER_PATH, NULL };
	char *envp[] = { NULL };

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		(void)syscall(SYS_execve, EXEC_HELPER_PATH, argv, envp);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) < 0)
		return -1;
	audit_pl("execve_ok", (long)pid, 0, status, NULL);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

static int step_execve_noent(void)
{
	pid_t pid;
	int status = 0;
	char *argv[] = { (char *)EXEC_NOENT_PATH, NULL };
	char *envp[] = { NULL };

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		(void)syscall(SYS_execve, EXEC_NOENT_PATH, argv, envp);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) < 0)
		return -1;
	audit_pl("execve_noent", (long)pid, 0, status, NULL);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 127)
		return -1;
	return 0;
}

static int step_wait4_exit1(void)
{
	pid_t pid;
	int status = 0;
	long wret;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
		_exit(1);

	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_pl("wait4_exit1", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != (long)pid)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 1)
		return -1;
	return 0;
}

static int step_kill_sigterm(void)
{
	pid_t pid;
	int status = 0;
	long wret;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		for (;;)
			pause();
	}

	if (kill(pid, SIGTERM) != 0)
		return -1;

	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_pl("kill_sigterm", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != (long)pid)
		return -1;
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
		return -1;
	return 0;
}

static int step_sigchld_default_wait(void)
{
	pid_t pid;
	int status = 0;
	long wret;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
		_exit(7);

	wret = (long)syscall(SYS_wait4, (long)pid, &status, 0, NULL);
	audit_pl("sigchld_default_wait", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != (long)pid)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 7)
		return -1;
	return 0;
}

static int step_reparent(void)
{
	pid_t mid;
	int status = 0;
	long wret;

	if (getpid() != 1)
	{
		audit_pl("reparent_skip", 0, 0, -1, "not_pid1");
		return 0;
	}

	mid = fork();
	if (mid < 0)
		return -1;
	if (mid == 0)
	{
		pid_t orphan = fork();

		if (orphan < 0)
			_exit(1);
		if (orphan == 0)
			_exit(99);
		_exit(0);
	}

	wret = (long)syscall(SYS_wait4, (long)mid, &status, 0, NULL);
	audit_pl("reparent_mid_wait", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret != (long)mid || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	status = 0;
	wret = (long)syscall(SYS_wait4, (long)-1, &status, 0, NULL);
	audit_pl("reparent_orphan_wait", wret, wret < 0 ? errno : 0, status, NULL);
	if (wret <= 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 99)
		return -1;
	return 0;
}

int main(void)
{
	if (step_wait4_exit42() != 0)
		return 1;
	if (step_wait4_any() != 0)
		return 1;
	if (step_wait4_wnohang() != 0)
		return 1;
	if (step_wait4_echild() != 0)
		return 1;
	if (step_execve_ok() != 0)
		return 1;
	if (step_execve_noent() != 0)
		return 1;
	if (step_wait4_exit1() != 0)
		return 1;
	if (step_kill_sigterm() != 0)
		return 1;
	if (step_sigchld_default_wait() != 0)
		return 1;
	if (step_reparent() != 0)
		return 1;

	(void)write(1, "[PROC_LIFECYCLEOK]\n", 19);
	return 0;
}
