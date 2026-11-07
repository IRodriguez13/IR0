/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: openat.c
 * Description: openat(2) + close(2) userspace selftest and ABI audit probe.
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

#ifndef OPEN_EXISTING_PATH
#define OPEN_EXISTING_PATH "/proc/self/status"
#endif

#define OPEN_NOENT_PATH "/sbin/ir0open_noent"

static void audit_open(unsigned step, const char *op, long ret, int err)
{
	char buf[96];
	unsigned int n = 0;
	unsigned int i;
	long mag;
	const char *prefix = "[LINUX_ABI_AUDIT][openat] step=";
	const char *mid_op = " op=";
	const char *mid_ret = " ret=";
	const char *mid_err = " errno=";

	while (prefix[n] != '\0' && n < sizeof(buf) - 1U)
	{
		buf[n] = prefix[n];
		n++;
	}
	if (n < sizeof(buf) - 1U)
		buf[n++] = (char)('0' + (step % 10U));
	i = 0;
	while (mid_op[i] != '\0' && n < sizeof(buf) - 1U)
		buf[n++] = mid_op[i++];
	i = 0;
	while (op[i] != '\0' && n < sizeof(buf) - 1U)
		buf[n++] = op[i++];
	i = 0;
	while (mid_ret[i] != '\0' && n < sizeof(buf) - 1U)
		buf[n++] = mid_ret[i++];
	if (ret < 0 && n < sizeof(buf) - 1U)
	{
		buf[n++] = '-';
		mag = -ret;
	}
	else
		mag = ret;
	if (mag == 0 && n < sizeof(buf) - 1U)
		buf[n++] = '0';
	else
	{
		char tmp[20];
		unsigned int t = 0;

		while (mag > 0 && t < sizeof(tmp))
		{
			tmp[t++] = (char)('0' + (mag % 10));
			mag /= 10;
		}
		while (t > 0 && n < sizeof(buf) - 1U)
			buf[n++] = tmp[--t];
	}
	i = 0;
	while (mid_err[i] != '\0' && n < sizeof(buf) - 1U)
		buf[n++] = mid_err[i++];
	if (err == 0 && n < sizeof(buf) - 1U)
		buf[n++] = '0';
	else
	{
		char tmp[12];
		unsigned int t = 0;
		int e = err;

		if (e < 0)
			e = -e;
		while (e > 0 && t < sizeof(tmp))
		{
			tmp[t++] = (char)('0' + (e % 10));
			e /= 10;
		}
		while (t > 0 && n < sizeof(buf) - 1U)
			buf[n++] = tmp[--t];
	}
	if (n < sizeof(buf) - 1U)
		buf[n++] = '\n';
	(void)write(1, buf, n);
}

int main(void)
{
	long fd;
	long ret;
	int pass;

	ksft_set_plan(4);

	fd = (long)syscall(SYS_openat, AT_FDCWD, (long)OPEN_EXISTING_PATH,
			   O_RDONLY, 0);
	audit_open(0, "open_existing", fd, fd < 0 ? errno : 0);
	pass = fd >= 3;
	ksft_test_result(pass, "open_existing");
	if (!pass)
		return ksft_exit();

	ret = (long)syscall(SYS_close, (int)fd);
	audit_open(1, "close_ok", ret, ret < 0 ? errno : 0);
	pass = ret == 0;
	ksft_test_result(pass, "close_ok");
	if (!pass)
		return ksft_exit();

	fd = (long)syscall(SYS_openat, AT_FDCWD, (long)OPEN_NOENT_PATH,
			   O_RDONLY, 0);
	audit_open(2, "open_noent", fd, errno);
	pass = fd == -1 && errno == 2;
	ksft_test_result(pass, "open_noent");
	if (!pass)
		return ksft_exit();

	ret = (long)syscall(SYS_close, -1);
	audit_open(3, "close_ebadf", ret, errno);
	pass = ret == -1 && errno == 9;
	ksft_test_result(pass, "close_ebadf");
	if (pass)
		(void)write(1, "[OPENATOK]\n", 11);
	return ksft_exit();
}
