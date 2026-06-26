/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: read_probe.c
 * Description: Minimal read(2) pipe/EOF/EBADF workload for Linux↔IR0 ABI audit
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PIPE_MSG "hello\n"
#define PIPE_MSG_LEN 6U
#define READ_BUF_SZ 1023U

static void audit_read_hex(unsigned step, const char *op, long ret, int err,
			   const unsigned char *data, unsigned len)
{
	char buf[256];
	char hex[PIPE_MSG_LEN * 2U + 1U];
	unsigned i;
	int n;

	if (data && len > 0U)
	{
		for (i = 0U; i < len; i++)
			sprintf(hex + (i * 2U), "%02x", data[i]);
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][read] step=%u op=%s ret=%ld errno=%d data_hex=%s\n",
			     step, op, ret, err, hex);
	}
	else
	{
		n = snprintf(buf, sizeof(buf),
			     "[LINUX_ABI_AUDIT][read] step=%u op=%s ret=%ld errno=%d\n",
			     step, op, ret, err);
	}
	if (n > 0)
		(void)write(1, buf, (size_t)n);
}

static int setup_pipe_on_stdin(void)
{
	int fds[2];
	ssize_t w;

	if (pipe(fds) < 0)
		return -1;
	w = write(fds[1], PIPE_MSG, PIPE_MSG_LEN);
	if (w != (ssize_t)PIPE_MSG_LEN)
	{
		close(fds[0]);
		close(fds[1]);
		return -1;
	}
	close(fds[1]);
	if (dup2(fds[0], 0) < 0)
	{
		close(fds[0]);
		return -1;
	}
	close(fds[0]);
	return 0;
}

int main(void)
{
	unsigned char buf[READ_BUF_SZ + 1U];
	long ret;

	if (setup_pipe_on_stdin() < 0)
		return 1;

	memset(buf, 0, sizeof(buf));
	ret = (long)syscall(SYS_read, 0, buf, READ_BUF_SZ);
	audit_read_hex(0, "read_pipe", ret, ret < 0 ? errno : 0, buf,
		       ret > 0 ? (unsigned)ret : 0U);
	if (ret != (long)PIPE_MSG_LEN)
		return 1;
	if (memcmp(buf, PIPE_MSG, PIPE_MSG_LEN) != 0)
		return 1;

	memset(buf, 0, sizeof(buf));
	ret = (long)syscall(SYS_read, 0, buf, READ_BUF_SZ);
	audit_read_hex(1, "read_eof", ret, ret < 0 ? errno : 0, NULL, 0U);
	if (ret != 0)
		return 1;

	ret = (long)syscall(SYS_read, -1, buf, 16);
	audit_read_hex(2, "read_ebadf", ret, errno, NULL, 0U);
	if (ret != -1 || errno != EBADF)
		return 1;

	(void)write(1, "[READOK]\n", 9);
	return 0;
}
