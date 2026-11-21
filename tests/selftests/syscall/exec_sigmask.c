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
/* arch/arm64/include/uapi/asm/unistd.h: __NR_execve is 221. */
#define SYS_execve 221
#else
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

static int is_child_arg(char **argv)
{
	const char *s;
	const char *want;

	if (!argv || !argv[1])
		return 0;
	s = argv[1];
	want = "child";
	while (*want != '\0' && *s == *want)
	{
		s++;
		want++;
	}
	return *want == '\0' && *s == '\0';
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
	char *av[3];
	int pid;

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
		av[1] = "child";
		av[2] = 0;
		(void)syscall(SYS_execve, (long)av[0], (long)av, 0L);
		_exit(1);
	}
	return reap(pid);
}

__attribute__((used, noinline)) static int exec_sigmask_main(char **argv)
{
	if (is_child_arg(argv))
		return run_child();
	return run_parent();
}

#ifdef IR0_SELFTEST_NOLIBC
/*
 * nolibc _start keeps the kernel entry argv in x1 and calls main.
 * This is the whole body of main: no C prologue can reuse x1.
 */
__asm__(
	".global main\n"
	".type main, %function\n"
	"main:\n"
	"	mov	x0, x1\n"
	"	b	exec_sigmask_main\n"
);
#else
int main(int argc, char **argv)
{
	(void)argc;
	return exec_sigmask_main(argv);
}
#endif
