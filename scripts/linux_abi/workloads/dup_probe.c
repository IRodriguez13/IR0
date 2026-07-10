/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dup_probe.c
 * Description: dup(2)/dup2(2) CLOEXEC-clear workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#ifndef DUP_EXISTING_PATH
/* Real VFS path: /proc opens return FD_PROC_BASE+ virtual fds that dup/fcntl
 * do not yet install into the process fd_table (see ARCH note in ABI board). */
#define DUP_EXISTING_PATH "/sbin/init"
#endif

static void audit_dup(unsigned step, const char *op, long ret, int err)
{
	char buf[160];
	int n;

	n = snprintf(buf, sizeof(buf),
		     "[LINUX_ABI_AUDIT][dup] step=%u op=%s ret=%ld errno=%d\n",
		     step, op, ret, err);
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

int main(void)
{
	int fd;
	int d;
	int flags;
	long ret;

	fd = open(DUP_EXISTING_PATH, O_RDONLY);
	audit_dup(0, "open_existing", (long)fd, fd < 0 ? errno : 0);
	if (fd < 0)
		return 1;

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0)
		return 1;
	flags = fcntl(fd, F_GETFD);
	audit_dup(1, "fcntl_set_cloexec", (long)flags, flags < 0 ? errno : 0);
	if (flags < 0 || !(flags & FD_CLOEXEC))
		return 1;

	d = dup(fd);
	audit_dup(2, "dup_clears_cloexec", (long)d, d < 0 ? errno : 0);
	if (d < 0)
		return 1;
	flags = fcntl(d, F_GETFD);
	audit_dup(3, "dup_getfd", (long)flags, flags < 0 ? errno : 0);
	if (flags < 0 || (flags & FD_CLOEXEC))
		return 1;
	if (close(d) != 0)
		return 1;

	d = dup2(fd, 20);
	audit_dup(4, "dup2_clears_cloexec", (long)d, d < 0 ? errno : 0);
	if (d != 20)
		return 1;
	flags = fcntl(d, F_GETFD);
	audit_dup(5, "dup2_getfd", (long)flags, flags < 0 ? errno : 0);
	if (flags < 0 || (flags & FD_CLOEXEC))
		return 1;
	if (close(d) != 0)
		return 1;

	ret = (long)dup2(-1, 21);
	audit_dup(6, "dup2_ebadf", ret, errno);
	if (ret != -1 || errno != EBADF)
		return 1;

	if (close(fd) != 0)
		return 1;

	(void)write(1, "[DUPOK]\n", 8);
	return 0;
}
