/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: namei_search.c
 * Description: A directory without execute permission hides every name
 *              inside it. stat returns EACCES whether or not that name
 *              exists, so the walk does not leak ENOENT.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

#ifndef SYS_newfstatat
#if defined(__x86_64__)
#define SYS_newfstatat 262
#else
#define SYS_newfstatat 79
#endif
#endif

#ifdef IR0_SELFTEST_NOLIBC
/* arch/arm64/include/uapi/asm/unistd.h */
#define SYS_mkdirat 34
#define SYS_fchmodat 53
#define SYS_setuid 146
#ifndef O_CREAT
#define O_CREAT 0100
#endif
#endif

/* errno(3) values. nolibc has no <errno.h>; not the kernel header. */
#define NAMEI_EACCES 13
#define NAMEI_ENOENT 2

static int stat_at(const char *path)
{
	unsigned char st[256];
	long ret;

	errno = 0;
	ret = syscall(SYS_newfstatat, AT_FDCWD, (long)path, (long)st, 0);
	if (ret == 0)
	{
		return 0;
	}
	return errno;
}

static int setup_tree(void)
{
	long vis;
	long ok;

	if (syscall(SYS_mkdirat, AT_FDCWD, (long)"/namei_ns", 0755) != 0)
	{
		return 0;
	}
	vis = syscall(SYS_openat, AT_FDCWD, (long)"/namei_ns/visible",
		      O_CREAT | O_WRONLY, 0644);
	if (vis < 0)
	{
		return 0;
	}
	if (close((int)vis) != 0)
	{
		return 0;
	}
	ok = syscall(SYS_openat, AT_FDCWD, (long)"/namei_ok",
		     O_CREAT | O_WRONLY, 0644);
	if (ok < 0)
	{
		return 0;
	}
	if (close((int)ok) != 0)
	{
		return 0;
	}
	/* Readable, so the name is visible, but not searchable. */
	if (syscall(SYS_fchmodat, AT_FDCWD, (long)"/namei_ns", 0644, 0) != 0)
	{
		return 0;
	}
	if (syscall(SYS_setuid, 1000) != 0)
	{
		return 0;
	}
	return 1;
}

int main(void)
{
	int pass;

	ksft_set_plan(1);
	if (!setup_tree())
	{
		ksft_test_result(0, "namei_search");
		return ksft_exit();
	}
	pass = stat_at("/namei_ns/visible") == NAMEI_EACCES &&
	       stat_at("/namei_ns/missing") == NAMEI_EACCES &&
	       stat_at("/namei_ok") == 0 &&
	       stat_at("/namei_missing") == NAMEI_ENOENT;
	ksft_test_result(pass, "namei_search");
	return ksft_exit();
}
