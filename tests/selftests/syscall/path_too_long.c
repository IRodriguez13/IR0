/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: path_too_long.c
 * Description: A path with no NUL in its first PATH_MAX bytes fails with
 *              ENAMETOOLONG (Linux getname()), not EFAULT: the pointer is
 *              valid, the name is too long.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#ifndef SYS_newfstatat
#if defined(__x86_64__)
#define SYS_newfstatat 262
#else
#define SYS_newfstatat 79
#endif
#endif

#define PROBE_PATH_MAX 4096
/* errno(3) ENAMETOOLONG; nolibc has no <errno.h>. */
#define LINUX_ENAMETOOLONG 36

/* PATH_MAX characters plus the NUL: one byte past what getname() accepts. */
static char long_path[PROBE_PATH_MAX + 1];
/* struct stat is 128 (arm64) or 144 (x86_64) bytes. */
static unsigned char stat_buf[256];

static int failed_with_enametoolong(long ret)
{
	if (ret == -1 && errno == LINUX_ENAMETOOLONG)
		return 1;
	ksft_print_num("ret", ret);
	ksft_print_num("errno", errno);
	return 0;
}

int main(void)
{
	/* volatile: a plain fill loop becomes memset(), which nolibc lacks. */
	volatile char *p = long_path;
	long ret;
	int i;

	p[0] = '/';
	for (i = 1; i < PROBE_PATH_MAX; i++)
		p[i] = 'a';
	p[PROBE_PATH_MAX] = '\0';

	ksft_set_plan(2);

	errno = 0;
	ret = (long)syscall(SYS_openat, AT_FDCWD, (long)long_path, O_RDONLY, 0);
	ksft_test_result(failed_with_enametoolong(ret),
			 "openat_path_max_enametoolong");

	errno = 0;
	ret = (long)syscall(SYS_newfstatat, AT_FDCWD, (long)long_path,
			    (long)stat_buf, 0);
	ksft_test_result(failed_with_enametoolong(ret),
			 "newfstatat_path_max_enametoolong");
	return ksft_exit();
}
