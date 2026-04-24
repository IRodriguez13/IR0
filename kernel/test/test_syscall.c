/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_syscall.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de syscalls
 */

#include "test/ktest_harness.h"
#include "syscalls.h"
#include <ir0/fcntl.h>
#include <string.h>

void ktest_syscall_getpid(void)
{
	KTEST_BEGIN("syscall_getpid");
	int64_t pid = sys_getpid();
	KASSERT_GE(pid, 0);
	KTEST_END();
}

void ktest_syscall_open_close(void)
{
	KTEST_BEGIN("syscall_open_close");
	int64_t fd = sys_open("/proc/uptime", 0, 0);
	KASSERT_GT(fd, 0);
	int64_t ret = sys_close((int)fd);
	KASSERT_EQ(ret, 0);
	KTEST_END();
}

void ktest_syscall_proc_read(void)
{
	KTEST_BEGIN("syscall_proc_read");
	int64_t fd = sys_open("/proc/uptime", 0, 0);
	KASSERT_GT(fd, 0);
	char buf[64];
	memset(buf, 0, sizeof(buf));
	int64_t n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	KASSERT_GE(n, 0);
	KASSERT_GT(n, 0);
	KASSERT(buf[0] >= '0' && buf[0] <= '9');
	KTEST_END();
}

void ktest_syscall_pipe(void)
{
	KTEST_BEGIN("syscall_pipe");
	int pipefd[2];
	int64_t ret = sys_pipe(pipefd);
	KASSERT_EQ(ret, 0);
	KASSERT_GE(pipefd[0], 0);
	KASSERT_GE(pipefd[1], 0);
	const char *msg = "x";
	int64_t w = sys_write(pipefd[1], msg, 1);
	KASSERT_EQ(w, 1);
	char c;
	int64_t r = sys_read(pipefd[0], &c, 1);
	KASSERT_EQ(r, 1);
	KASSERT_EQ(c, 'x');
	sys_close(pipefd[0]);
	sys_close(pipefd[1]);
	KTEST_END();
}
