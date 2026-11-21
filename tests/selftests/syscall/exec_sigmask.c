/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: exec_sigmask.c
 * Description: execve keeps the blocked signal set.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
/* arch/arm64/include/uapi/asm/unistd.h: __NR_execve 221, __NR_unlinkat 35. */
#define SYS_execve 221
#define SYS_unlinkat 35
#define O_CREAT 0100
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

/*
 * signal_mask stores SIGNAL_MASK(N) = 1<<N. rt_sigprocmask(sigsetsize=8)
 * crosses ir0_sigword_to_linux() / linux_sigword_to_ir0(), a shift of one,
 * so userspace sees Linux bit N-1.
 */
#define SIGUSR1_LINUX_BIT (1UL << (SIGUSR1 - 1))

/*
 * The nolibc entry does not pass argv, so the re-executed image learns it is
 * the child from a marker file the parent creates before execve.
 */
#define CHILD_MARKER "/es_child"

static int take_child_marker(void)
{
	int fd;

	fd = open(CHILD_MARKER, O_RDONLY);
	if (fd < 0)
		return 0;
	(void)close(fd);
	(void)syscall(SYS_unlinkat, (long)AT_FDCWD, (long)CHILD_MARKER, 0L);
	return 1;
}

static int make_child_marker(void)
{
	int fd;

	fd = (int)syscall(SYS_openat, (long)AT_FDCWD, (long)CHILD_MARKER,
			  (long)(O_WRONLY | O_CREAT), 0644L);
	if (fd < 0)
		return -1;
	(void)close(fd);
	return 0;
}

static void zero_bytes(void *p, int n)
{
	volatile unsigned char *z;
	int i;

	z = (volatile unsigned char *)p;
	for (i = 0; i < n; i++)
		z[i] = 0;
}

static int run_child(void)
{
	unsigned long old;
	long ret;
	int pass;

	zero_bytes(&old, (int)sizeof(old));
	ret = syscall(SYS_rt_sigprocmask, (long)SIG_BLOCK, 0L, (long)&old, 8L);
	pass = ret == 0 && (old & SIGUSR1_LINUX_BIT) != 0;
	ksft_set_plan(1);
	ksft_test_result(pass, "exec_sigmask");
	return ksft_exit();
}

static int reap(int pid)
{
	int status;
	int wr;

	if (pid <= 0)
		return 1;
	status = 0;
#ifdef IR0_SELFTEST_NOLIBC
	wr = wait4(pid, &status, 0, 0);
#else
	wr = waitpid(pid, &status, 0);
#endif
	if (wr < 0 || !WIFEXITED(status))
		return 1;
	return WEXITSTATUS(status);
}

static int run_parent(void)
{
	unsigned long block;
	char *av[2];
	int pid;

	if (make_child_marker() != 0)
		return 1;
	block = SIGUSR1_LINUX_BIT;
	if (syscall(SYS_rt_sigprocmask, (long)SIG_BLOCK, (long)&block, 0L,
		    8L) != 0)
		return 1;
	pid = fork();
	if (pid < 0)
		return 1;
	if (pid == 0)
	{
		av[0] = "/proc/self/exe";
		av[1] = 0;
		(void)syscall(SYS_execve, (long)av[0], (long)av, 0L);
		_exit(1);
	}
	return reap(pid);
}

int main(void)
{
	if (take_child_marker())
		return run_child();
	return run_parent();
}
