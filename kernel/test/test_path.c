/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_path.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests de path (normalize_path, join_paths, is_absolute_path, get_parent_path)
 * Sin proceso; solo lógica de rutas.
 */

#include "test/ktest_harness.h"
#include <ir0/path.h>
#include <string.h>

static void assert_str_eq(const char *got, const char *expected)
{
	size_t n = strlen(expected);
	KASSERT(strlen(got) == n);
	for (size_t i = 0; i <= n; i++)
		KASSERT(got[i] == expected[i]);
}

void ktest_path(void)
{
	char buf[256];

	KTEST_BEGIN("path");

	KASSERT(normalize_path("/", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/");

	KASSERT(normalize_path("/foo/bar", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/foo/bar");

	KASSERT(normalize_path("/foo/./bar", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/foo/bar");

	KASSERT(normalize_path("/foo/../bar", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/bar");

	KASSERT(is_absolute_path("/") != 0);
	KASSERT(is_absolute_path("/tmp") != 0);
	KASSERT(is_absolute_path("tmp") == 0);
	KASSERT(is_absolute_path("") == 0);

	KASSERT(join_paths("/", "foo", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/foo");

	KASSERT(join_paths("/foo", "bar", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/foo/bar");

	KASSERT(get_parent_path("/foo/bar", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/foo");

	KASSERT(get_parent_path("/", buf, sizeof(buf)) == 0);
	assert_str_eq(buf, "/");

	KTEST_END();
}
