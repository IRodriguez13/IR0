/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_procfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de procfs
 */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include "process.h"
#include <ir0/procfs.h>
#include <string.h>

void ktest_procfs_uptime(void)
{
	KTEST_BEGIN("procfs_uptime");
	int64_t fd = sys_open("/proc/uptime", 0, 0);
	KASSERT_GT(fd, 0);
	char buf[128];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GE(n, 0);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] >= '0' && buf[0] <= '9');
	KTEST_END();
}

void ktest_procfs_pid_status(void)
{
	pid_t pid = -1;
	const char *name;
	int fd;
	char buf[256];
	int n;

	KTEST_BEGIN("procfs_pid_status");
	if (!current_process)
	{
		KTEST_END();
		return;
	}

	name = proc_resolve_path("/proc/status", &pid);
	KASSERT(name != NULL);
	KASSERT(strcmp(name, "status") == 0);

	fd = proc_open("/proc/status", 0);
	KASSERT_GT(fd, 0);

	memset(buf, 0, sizeof(buf));
	n = proc_read(fd, buf, sizeof(buf) - 1, 0);
	KASSERT_GT(n, 0);
	KASSERT(strstr(buf, "\t") != NULL);

	{
		int64_t sfd = sys_open("/proc/status", 0, 0);
		KASSERT_GT(sfd, 0);
		sys_close((int)sfd);
	}
	KTEST_END();
}
