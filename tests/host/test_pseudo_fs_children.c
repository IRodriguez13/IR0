/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_pseudo_fs_children.c
 * Description: Host tests for pseudo_fs path children probe and registry readdir.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/pseudo_fs.h>
#include <ir0/errno.h>
#include <ir0/vfs.h>
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

static const struct vfs_dirent *find_dirent(const struct vfs_dirent *entries,
                                            int n, const char *name)
{
	for (int i = 0; i < n; i++)
	{
		if (strcmp(entries[i].name, name) == 0)
			return &entries[i];
	}

	return NULL;
}

void test_pseudo_fs_path_has_children_and_collect(void)
{
	struct vfs_dirent entries[16];
	const struct vfs_dirent *de;
	int rc;
	int n;

	TEST_BEGIN("pseudo_fs_path_has_children_collect");

	pseudo_fs_reset();

	ASSERT_EQ(pseudo_fs_path_has_children(NULL), 0);
	ASSERT_EQ(pseudo_fs_path_has_children("/sys"), 0);
	ASSERT_EQ(pseudo_fs_path_has_children("/proc"), 0);

	rc = pseudo_fs_register("/sys", "kernel", &mock_ops, "k");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/sys", "kernel/metrics", &mock_ops, "m");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/sys", "kernel/metrics/detail", &mock_ops, "d");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/sys", "devices", &mock_ops, "dev");
	ASSERT_EQ(rc, 0);

	ASSERT_EQ(pseudo_fs_path_has_children("/sys"), 1);
	ASSERT_EQ(pseudo_fs_path_has_children("/sys/kernel"), 1);
	ASSERT_EQ(pseudo_fs_path_has_children("/sys/kernel/metrics"), 1);
	ASSERT_EQ(pseudo_fs_path_has_children("/sys/kernel/metrics/detail"), 0);
	ASSERT_EQ(pseudo_fs_path_has_children("/sys/devices"), 0);
	ASSERT_EQ(pseudo_fs_path_has_children("/sys/missing"), 0);

	rc = pseudo_fs_register("/proc", "cpuinfo", &mock_ops, "c");
	ASSERT_EQ(rc, 0);
	rc = pseudo_fs_register("/proc", "self/status", &mock_ops, "s");
	ASSERT_EQ(rc, 0);

	ASSERT_EQ(pseudo_fs_path_has_children("/proc"), 1);
	ASSERT_EQ(pseudo_fs_path_has_children("/proc/self"), 1);
	ASSERT_EQ(pseudo_fs_path_has_children("/proc/cpuinfo"), 0);

	ASSERT_EQ(pseudo_fs_collect_registry_children(NULL, entries, 16, 0), -EINVAL);
	ASSERT_EQ(pseudo_fs_collect_registry_children("/sys", NULL, 16, 0), -EINVAL);
	ASSERT_EQ(pseudo_fs_collect_registry_children("/sys", entries, 0, 0), -EINVAL);
	ASSERT_EQ(pseudo_fs_collect_registry_children("/sys", entries, 16, -1), -EINVAL);

	memset(entries, 0, sizeof(entries));
	n = pseudo_fs_collect_registry_children("/sys", entries, 16, 0);
	ASSERT_EQ(n, 2);

	de = find_dirent(entries, n, "kernel");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_REG);

	de = find_dirent(entries, n, "devices");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_REG);

	memset(entries, 0, sizeof(entries));
	n = pseudo_fs_collect_registry_children("/sys/kernel", entries, 16, 0);
	ASSERT_EQ(n, 1);

	de = find_dirent(entries, n, "metrics");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_REG);

	memset(entries, 0, sizeof(entries));
	n = pseudo_fs_collect_registry_children("/sys/kernel/metrics", entries, 16, 0);
	ASSERT_EQ(n, 1);

	de = find_dirent(entries, n, "detail");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_REG);

	memset(entries, 0, sizeof(entries));
	n = pseudo_fs_collect_registry_children("/proc", entries, 16, 0);
	ASSERT_EQ(n, 2);

	de = find_dirent(entries, n, "cpuinfo");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_REG);

	de = find_dirent(entries, n, "self");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_DIR);

	memset(entries, 0, sizeof(entries));
	n = pseudo_fs_collect_registry_children("/proc/self", entries, 16, 0);
	ASSERT_EQ(n, 1);

	de = find_dirent(entries, n, "status");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_REG);

	memset(entries, 0, sizeof(entries));
	entries[0].type = DT_REG;
	strncpy(entries[0].name, "seed", sizeof(entries[0].name) - 1);
	n = pseudo_fs_collect_registry_children("/sys", entries, 16, 1);
	ASSERT_EQ(n, 3);
	ASSERT_STR_EQ(entries[0].name, "seed");
	ASSERT(find_dirent(entries, n, "kernel") != NULL);
	ASSERT(find_dirent(entries, n, "devices") != NULL);

	/* Nested-only path: no leaf at /sys/nested, so intermediate names are DT_DIR. */
	pseudo_fs_reset();
	rc = pseudo_fs_register("/sys", "nested/only/leaf", &mock_ops, "x");
	ASSERT_EQ(rc, 0);

	memset(entries, 0, sizeof(entries));
	n = pseudo_fs_collect_registry_children("/sys", entries, 16, 0);
	ASSERT_EQ(n, 1);

	de = find_dirent(entries, n, "nested");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_DIR);

	memset(entries, 0, sizeof(entries));
	n = pseudo_fs_collect_registry_children("/sys/nested", entries, 16, 0);
	ASSERT_EQ(n, 1);

	de = find_dirent(entries, n, "only");
	ASSERT(de != NULL);
	ASSERT_EQ(de->type, DT_DIR);

	pseudo_fs_reset();
	TEST_END();
}
