/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: dyn_hello_stage.c
 * Description: Static PID 1 of the dyn_hello selftest. The MINIX v1 test
 *              disk keeps 14-character names, too short for
 *              ld-musl-<arch>.so.1, so this mounts a tmpfs on /lib, copies
 *              the loader and libdynhello.so there under their real names
 *              and execs the dynamically linked /dyn/hello.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#if defined(__aarch64__)
#define DYN_LDSO "/lib/ld-musl-aarch64.so.1"
#else
#define DYN_LDSO "/lib/ld-musl-x86_64.so.1"
#endif

static char g_buf[4096];

/* Copy host-staged @from to @to; 0 or -1. */
static int copy_file(const char *from, const char *to)
{
	long in = syscall(SYS_openat, AT_FDCWD, (long)from, O_RDONLY, 0);
	long out;
	long n;

	if (in < 0)
		return -1;
	out = syscall(SYS_openat, AT_FDCWD, (long)to, O_WRONLY | O_CREAT, 0755);
	if (out < 0)
		return -1;
	while ((n = read((int)in, g_buf, sizeof(g_buf))) > 0)
	{
		if (write((int)out, g_buf, (size_t)n) != n)
			return -1;
	}
	close((int)in);
	close((int)out);
	return n < 0 ? -1 : 0;
}

int main(void)
{
	static char *const argv[] = { "/dyn/hello", 0 };
	static char *const envp[] = { 0 };

	(void)syscall(SYS_mkdirat, AT_FDCWD, (long)"/lib", 0755);
	if (mount("none", "/lib", "tmpfs", 0, 0) != 0 ||
	    copy_file("/dyn/ldso", DYN_LDSO) != 0 ||
	    copy_file("/dyn/libdynhello.so", "/lib/libdynhello.so") != 0)
	{
		ksft_set_plan(1);
		ksft_test_result(0, "dyn_hello_stage");
		return ksft_exit();
	}
	(void)syscall(SYS_execve, (long)argv[0], (long)argv, (long)envp);
	ksft_set_plan(1);
	ksft_test_result(0, "dyn_hello_execve");
	return ksft_exit();
}
