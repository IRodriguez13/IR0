/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: path_too_long.c
 * Description: A path with no NUL in its first PATH_MAX bytes fails with
 *              ENAMETOOLONG (Linux getname()), not EFAULT: the pointer is
 *              valid, the name is too long. mount(2) copies its source and
 *              filesystem type with strndup_user(.., PATH_MAX): longer
 *              strings fail with EINVAL, and a type that fits but names
 *              no filesystem fails with ENODEV.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <fcntl.h>
#include <sys/mount.h>
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
#define LINUX_EINVAL 22
#define LINUX_ENODEV 19

/* PATH_MAX characters plus the NUL: one byte past what getname() accepts. */
static char long_path[PROBE_PATH_MAX + 1];
/* struct stat is 128 (arm64) or 144 (x86_64) bytes. */
static unsigned char stat_buf[256];

static int failed_with(long ret, int want)
{
	if (ret == -1 && errno == want)
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

	ksft_set_plan(6);

	errno = 0;
	ret = (long)syscall(SYS_openat, AT_FDCWD, (long)long_path, O_RDONLY, 0);
	ksft_test_result(failed_with(ret, LINUX_ENAMETOOLONG),
			 "openat_path_max_enametoolong");

	errno = 0;
	ret = (long)syscall(SYS_newfstatat, AT_FDCWD, (long)long_path,
			    (long)stat_buf, 0);
	ksft_test_result(failed_with(ret, LINUX_ENAMETOOLONG),
			 "newfstatat_path_max_enametoolong");

	errno = 0;
	ret = mount("none", "/", long_path, 0, 0);
	ksft_test_result(failed_with(ret, LINUX_EINVAL),
			 "mount_fstype_path_max_einval");

	errno = 0;
	ret = mount(long_path, "/", "tmpfs", 0, 0);
	ksft_test_result(failed_with(ret, LINUX_EINVAL),
			 "mount_source_path_max_einval");

	errno = 0;
	ret = mount("none", long_path, "tmpfs", 0, 0);
	ksft_test_result(failed_with(ret, LINUX_ENAMETOOLONG),
			 "mount_target_path_max_enametoolong");

	/* Longer than any filesystem name, far shorter than PATH_MAX. */
	errno = 0;
	ret = mount("none", "/", "ir0_selftest_no_such_filesystem_type_x", 0, 0);
	ksft_test_result(failed_with(ret, LINUX_ENODEV),
			 "mount_fstype_unknown_enodev");
	return ksft_exit();
}
