/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_path_resolve_at.c
 * Description: Host test — runsv relative supervise paths via join_paths
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/path.h>

static int resolve_relative(const char *dir_base, const char *rel,
                            char *resolved, size_t resolved_sz)
{
	if (!dir_base || !rel || !resolved || resolved_sz == 0)
		return -1;
	if (rel[0] == '/')
		return normalize_path(rel, resolved, resolved_sz);
	return join_paths(dir_base, rel, resolved, resolved_sz);
}

void test_path_resolve_at(void)
{
	char resolved[256];
	int rc;

	TEST_BEGIN("path_resolve_at_supervise_lock");
	rc = resolve_relative("/etc/runit/sv/console", "supervise/lock",
	                      resolved, sizeof(resolved));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(resolved, "/etc/runit/sv/console/supervise/lock");
	TEST_END();

	TEST_BEGIN("path_resolve_at_supervise_control");
	rc = resolve_relative("/etc/runit/sv/logger", "supervise/control",
	                      resolved, sizeof(resolved));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(resolved, "/etc/runit/sv/logger/supervise/control");
	TEST_END();
}
