/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: openat_probe.c
 * Description: openat(2) + close(2) workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef OPEN_EXISTING_PATH
#define OPEN_EXISTING_PATH "/proc/self/status"
#endif

#define OPEN_NOENT_PATH "/sbin/ir0open_noent"

static void audit_open(unsigned step, const char *op, long ret, int err)
{
	char buf[256];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][openat] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	long fd;
	long ret;

	fd = (long)syscall(SYS_openat, AT_FDCWD, OPEN_EXISTING_PATH, O_RDONLY, 0);
	audit_open(0, "open_existing", fd, fd < 0 ? errno : 0);
	if (fd < 3)
		return 1;

	ret = (long)syscall(SYS_close, (int)fd);
	audit_open(1, "close_ok", ret, ret < 0 ? errno : 0);
	if (ret != 0)
		return 1;

	fd = (long)syscall(SYS_openat, AT_FDCWD, OPEN_NOENT_PATH, O_RDONLY, 0);
	audit_open(2, "open_noent", fd, errno);
	if (fd != -1)
		return 1;

	ret = (long)syscall(SYS_close, -1);
	audit_open(3, "close_ebadf", ret, errno);
	if (ret != -1 || errno != EBADF)
		return 1;

	(void)write(1, "[OPENATOK]\n", 11);
	return 0;
}
