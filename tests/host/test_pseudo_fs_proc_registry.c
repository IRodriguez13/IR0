/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_pseudo_fs_proc_registry.c
 * Description: Host tests for /proc pseudo_fs registry path layout.
 */

#include "test_harness_ir0.h"
#include <ir0/pseudo_fs.h>
#include <ir0/errno.h>
#include <string.h>

static int64_t mock_proc_read(void *ctx, char *buf, size_t count, off_t *off)
{
	const char *s;
	size_t len;
	size_t n;

	if (!buf || !off)
		return -EINVAL;

	s = (const char *)ctx;
	if (!s)
		return -EINVAL;

	len = strlen(s);
	if ((size_t)*off >= len)
		return 0;

	n = len - (size_t)*off;
	if (n > count)
		n = count;

	memcpy(buf, s + (size_t)*off, n);
	*off += (off_t)n;
	return (int64_t)n;
}

static const pseudo_fs_ops_t mock_proc_ops = {
	.read = mock_proc_read,
};

typedef struct mock_pid_ctx
{
	int kind;
} mock_pid_ctx_t;

static mock_pid_ctx_t g_mock_pid_ctx;

static int mock_proc_pid_match(const char *path, void **out_ctx)
{
	if (!out_ctx || !path)
		return -EINVAL;

	if (strcmp(path, "/proc/42/status") != 0 &&
	    strcmp(path, "/proc/42/cmdline") != 0)
	{
		return -ENOENT;
	}

	g_mock_pid_ctx.kind = (strstr(path, "cmdline") != NULL) ? 1 : 0;
	*out_ctx = &g_mock_pid_ctx;
	return 0;
}

static int64_t mock_proc_pid_read(void *ctx, char *buf, size_t count, off_t *off)
{
	const char *s;

	(void)ctx;
	(void)off;
	s = "init\tR\t42\t1\t0\t0\n";
	if (!buf || count == 0)
		return 0;
	strncpy(buf, s, count - 1);
	buf[count - 1] = '\0';
	return (int64_t)strlen(buf);
}

static int64_t mock_proc_pid_close(void *ctx)
{
	(void)ctx;
	return 0;
}

static const pseudo_fs_ops_t mock_proc_pid_ops = {
	.read = mock_proc_pid_read,
	.close = mock_proc_pid_close,
};

void test_pseudo_fs_proc_registry_paths(void)
{
	const pseudo_fs_entry_t *e;
	int rc;
	int fd;
	char out[128];
	int64_t n;

	TEST_BEGIN("pseudo_fs_proc_registry_paths");

	pseudo_fs_reset();

	rc = pseudo_fs_register("/proc", "cpuinfo", &mock_proc_ops, "cpuinfo-ok");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/proc", "uptime", &mock_proc_ops, "12345");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/proc", "version", &mock_proc_ops, "IR0\tbuild");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/proc", "blockdevices", &mock_proc_ops, "block");
	ASSERT_EQ(rc, 0);

	e = pseudo_fs_lookup("/proc/cpuinfo");
	ASSERT(e != NULL);
	ASSERT_STR_EQ(e->full_path, "/proc/cpuinfo");

	e = pseudo_fs_lookup("/proc/uptime");
	ASSERT(e != NULL);
	ASSERT_STR_EQ(e->full_path, "/proc/uptime");

	e = pseudo_fs_lookup("/proc/version");
	ASSERT(e != NULL);
	ASSERT_STR_EQ(e->full_path, "/proc/version");

	e = pseudo_fs_lookup("/proc/blockdevices");
	ASSERT(e != NULL);
	ASSERT_STR_EQ(e->full_path, "/proc/blockdevices");

	rc = pseudo_fs_open_path("/proc/cpuinfo", 0, &fd);
	ASSERT_EQ(rc, 0);

	memset(out, 0, sizeof(out));
	n = pseudo_fs_read_fd(fd, out, sizeof(out) - 1, 0);
	ASSERT_EQ(n, 10);
	ASSERT_STR_EQ(out, "cpuinfo-ok");

	rc = pseudo_fs_register_dynamic("/proc", mock_proc_pid_match, &mock_proc_pid_ops);
	ASSERT_EQ(rc, 0);

	rc = pseudo_fs_open_path("/proc/42/status", 0, &fd);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(fd, 1700);

	memset(out, 0, sizeof(out));
	n = pseudo_fs_read_fd(fd, out, sizeof(out) - 1, 0);
	ASSERT_EQ(n, (int64_t)strlen("init\tR\t42\t1\t0\t0\n"));
	ASSERT(strstr(out, "\t") != NULL);
	pseudo_fs_close_fd(fd);

	pseudo_fs_reset();
	TEST_END();
}
