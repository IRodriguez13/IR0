/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_pseudo_fs_registry.c
 * Description: Host tests for pseudo_fs register, longest-prefix lookup, and read ops.
 */

#include "test_harness_ir0.h"
#include <ir0/pseudo_fs.h>
#include <ir0/errno.h>
#include <string.h>

static int64_t mock_read(void *ctx, char *buf, size_t count, off_t *off)
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

static const pseudo_fs_ops_t mock_ops = {
	.read = mock_read,
};

void test_pseudo_fs_register_lookup_read(void)
{
	const pseudo_fs_entry_t *e;
	int rc;
	int fd;
	char out[64];
	int64_t n;

	TEST_BEGIN("pseudo_fs_register_lookup_read");

	pseudo_fs_reset();

	rc = pseudo_fs_register("/sys", "kernel/metrics", &mock_ops, "outer");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/sys", "kernel/metrics/detail", &mock_ops, "inner");
	ASSERT_EQ(rc, 0);

	e = pseudo_fs_lookup("/sys/kernel/metrics/detail");
	ASSERT(e != NULL);
	ASSERT_STR_EQ(e->full_path, "/sys/kernel/metrics/detail");
	ASSERT_EQ(e->ctx, (void *)"inner");

	e = pseudo_fs_lookup("/sys/kernel/metrics");
	ASSERT(e != NULL);
	ASSERT_STR_EQ(e->full_path, "/sys/kernel/metrics");
	ASSERT_EQ(e->ctx, (void *)"outer");

	rc = pseudo_fs_register("/sys", "kernel/metrics", &mock_ops, "dup");
	ASSERT_EQ(rc, -EEXIST);

	rc = pseudo_fs_open_path("/sys/kernel/metrics/detail", 0, &fd);
	ASSERT_EQ(rc, 0);

	memset(out, 0, sizeof(out));
	n = pseudo_fs_read_fd(fd, out, sizeof(out) - 1, 0);
	ASSERT_EQ(n, 5);
	ASSERT_STR_EQ(out, "inner");

	memset(out, 0, sizeof(out));
	n = pseudo_fs_read_fd(fd, out, sizeof(out) - 1, 5);
	ASSERT_EQ(n, 0);

	pseudo_fs_reset();
	TEST_END();
}
