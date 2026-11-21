/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: pipe_sz.c
 * Description: F_GETPIPE_SZ returns the pipe capacity. F_SETPIPE_SZ of
 *              that size succeeds; any other size fails.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
/* arch/arm64/include/uapi/asm/unistd.h __NR_fcntl */
#define SYS_fcntl 25
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/* Linux fcntl.h: absent from nolibc. Do not redefine musl's copies. */
#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif
#ifndef F_GETPIPE_SZ
#define F_GETPIPE_SZ 1032
#endif

static int open_pipe(int fds[2])
{
#ifdef IR0_SELFTEST_NOLIBC
	return pipe2(fds, 0);
#else
	return pipe(fds);
#endif
}

/* musl fcntl(2) on x86. nolibc syscall() returns -1 and sets errno. */
static long fcntl_cmd(int fd, int cmd, long arg)
{
#ifdef IR0_SELFTEST_NOLIBC
	return syscall(SYS_fcntl, fd, cmd, arg);
#else
	return fcntl(fd, cmd, arg);
#endif
}

int main(void)
{
	int fds[2];
	int pass;
	long got;
	long same;
	long bigger;

	ksft_set_plan(1);
	pass = 0;

	if (open_pipe(fds) < 0)
		goto out;
	got = fcntl_cmd(fds[0], F_GETPIPE_SZ, 0);
	if (got != 4096)
		goto out;
	same = fcntl_cmd(fds[0], F_SETPIPE_SZ, 4096);
	if (same != 4096)
		goto out;
	bigger = fcntl_cmd(fds[0], F_SETPIPE_SZ, 8192);
	if (bigger != -1 || errno != 22)
		goto out;
	pass = 1;
out:
	ksft_test_result(pass, "pipe_sz");
	return ksft_exit();
}
