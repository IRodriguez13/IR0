/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: pt_interp_probe.c
 * Description: fork+exec a PT_INTERP ET_EXEC (pid1 for QEMU smoke)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PT_INTERP_MAIN_PATH
#define PT_INTERP_MAIN_PATH "/sbin/dmain"
#endif

int main(void)
{
	pid_t pid;
	int st;
	char *argv[] = { (char *)PT_INTERP_MAIN_PATH, NULL };
	char *envp[] = { NULL };

	(void)write(1, "[PTINTERP] start\n", 17);

	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0)
	{
		(void)syscall(SYS_execve, PT_INTERP_MAIN_PATH, argv, envp);
		(void)write(1, "[PTINTERP] execve_fail\n", 23);
		_exit(127);
	}

	st = 0;
	if (waitpid(pid, &st, 0) < 0)
		return 1;
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
	{
		char buf[64];
		int n;

		n = snprintf(buf, sizeof(buf),
			     "[PTINTERP] child_status=0x%x errno=%d\n", st, errno);
		if (n > 0)
			(void)write(1, buf, (size_t)n);
		return 1;
	}

	(void)write(1, "[PTINTERPOK]\n", 13);
	return 0;
}
