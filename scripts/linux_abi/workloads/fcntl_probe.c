/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fcntl_probe.c
 * Description: fcntl(F_GETFD/F_SETFD) ABI audit probe
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static void audit_fc(unsigned step, const char *op, long ret, int err)
{
	char buf[160];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][fcntl] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	int fd;
	long flags;

	fd = open("/proc/uptime", O_RDONLY);
	audit_fc(0, "open", (long)fd, fd < 0 ? errno : 0);
	if (fd < 0)
		return 1;

	flags = (long)fcntl(fd, F_GETFD);
	audit_fc(1, "fcntl_getfd", flags, flags < 0 ? errno : 0);
	if (flags < 0)
		return 1;

	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0)
	{
		audit_fc(2, "fcntl_setfd", -1L, errno);
		return 1;
	}
	audit_fc(2, "fcntl_setfd", 0L, 0);

	flags = (long)fcntl(fd, F_GETFD);
	audit_fc(3, "fcntl_getfd_cloexec", flags, flags < 0 ? errno : 0);
	if (flags < 0 || !(flags & FD_CLOEXEC))
		return 1;

	flags = (long)fcntl(fd, F_GETFL);
	audit_fc(4, "fcntl_getfl", flags, flags < 0 ? errno : 0);
	if (flags < 0 || ((int)flags & O_ACCMODE) != O_RDONLY)
		return 1;

	{
		int d;

		d = fcntl(fd, F_DUPFD, 20);
		audit_fc(5, "fcntl_dupfd", (long)d, d < 0 ? errno : 0);
		if (d < 20)
			return 1;
		flags = (long)fcntl(d, F_GETFD);
		audit_fc(6, "fcntl_dupfd_getfd", flags, flags < 0 ? errno : 0);
		if (flags < 0 || (flags & FD_CLOEXEC))
			return 1;
		if (close(d) != 0)
			return 1;

		d = fcntl(fd, F_DUPFD_CLOEXEC, 21);
		audit_fc(7, "fcntl_dupfd_cloexec", (long)d, d < 0 ? errno : 0);
		if (d < 21)
			return 1;
		flags = (long)fcntl(d, F_GETFD);
		audit_fc(8, "fcntl_dupfd_cloexec_getfd", flags,
			 flags < 0 ? errno : 0);
		if (flags < 0 || !(flags & FD_CLOEXEC))
			return 1;
		if (close(d) != 0)
			return 1;
	}

	if (close(fd) != 0)
		return 1;

	(void)write(1, "[FCNTLOK]\n", 10);
	return 0;
}
