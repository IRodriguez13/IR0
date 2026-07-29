/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_path_chroot.c
 * Description: Host test — chroot root apply / under / getcwd strip
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/path.h>

void test_path_chroot(void)
{
	char out[256];
	int rc;

	TEST_BEGIN("path_chroot_apply_identity_root");
	rc = ir0_path_apply_root("/", "/etc/passwd", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/etc/passwd");
	rc = ir0_path_apply_root(NULL, "/tmp", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/tmp");
	TEST_END();

	TEST_BEGIN("path_chroot_apply_jail");
	rc = ir0_path_apply_root("/jail", "/etc/passwd", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/jail/etc/passwd");
	rc = ir0_path_apply_root("/jail", "/", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/jail");
	rc = ir0_path_apply_root("/jail", "/./tmp/../etc", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/jail/etc");
	TEST_END();

	TEST_BEGIN("path_chroot_under_root");
	ASSERT_EQ(ir0_path_under_root("/jail", "/jail"), 1);
	ASSERT_EQ(ir0_path_under_root("/jail", "/jail/tmp"), 1);
	ASSERT_EQ(ir0_path_under_root("/jail", "/jailx"), 0);
	ASSERT_EQ(ir0_path_under_root("/jail", "/other"), 0);
	ASSERT_EQ(ir0_path_under_root("/", "/anything"), 1);
	TEST_END();

	TEST_BEGIN("path_chroot_getcwd_visible");
	rc = ir0_path_getcwd_visible("/jail", "/jail", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/");
	rc = ir0_path_getcwd_visible("/jail", "/jail/tmp", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/tmp");
	rc = ir0_path_getcwd_visible("/jail", "/home", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/home");
	rc = ir0_path_getcwd_visible("/", "/home/ivan", out, sizeof(out));
	ASSERT_EQ(rc, 0);
	ASSERT_STR_EQ(out, "/home/ivan");
	TEST_END();
}
