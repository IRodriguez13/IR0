/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_bpath_fold.c
 * Description: Host checks for boot-path VFS fold prefixes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

extern int ir0_bpath_is_reserved_early(const char *path);
extern int ir0_bpath_is_foldable(const char *path);
extern int ir0_bpath_is_tmp(const char *path);

void test_bpath_fold(void)
{
	TEST_BEGIN("bpath_fold");

	ASSERT_EQ(ir0_bpath_is_foldable("/tmp/a"), 1);
	ASSERT_EQ(ir0_bpath_is_foldable("/tmp/"), 0);
	ASSERT_EQ(ir0_bpath_is_foldable("/run/svc"), 1);
	ASSERT_EQ(ir0_bpath_is_foldable("/run"), 0);
	ASSERT_EQ(ir0_bpath_is_foldable("/etc/passwd"), 0);
	ASSERT_EQ(ir0_bpath_is_tmp("/tmp/x"), 1);
	ASSERT_EQ(ir0_bpath_is_tmp("/run/x"), 0);

	ASSERT_EQ(ir0_bpath_is_reserved_early("/"), 1);
	ASSERT_EQ(ir0_bpath_is_reserved_early("/init"), 1);
	ASSERT_EQ(ir0_bpath_is_reserved_early("/dev"), 1);
	ASSERT_EQ(ir0_bpath_is_reserved_early("/dev/tty"), 1);
	ASSERT_EQ(ir0_bpath_is_reserved_early("/bpivot"), 1);
	ASSERT_EQ(ir0_bpath_is_reserved_early("/bpivot/new"), 1);
	ASSERT_EQ(ir0_bpath_is_reserved_early("/usr"), 0);
	ASSERT_EQ(ir0_bpath_is_foldable("/dev/null"), 0);
	ASSERT_EQ(ir0_bpath_is_foldable("/usr/bin/sh"), 0);

	TEST_END();
}
