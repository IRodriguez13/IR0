/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_stat_user_abi.c
 * Description: Host test — musl x86_64 struct stat userspace layout size
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stdint.h>

/* Mirror of includes/ir0/stat_user.h (avoid kernel types.h on host). */
struct musl_x64_stat_abi
{
	uint64_t st_dev;
	uint64_t st_ino;
	unsigned long st_nlink;
	unsigned st_mode;
	unsigned st_uid;
	unsigned st_gid;
	unsigned __pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;
	struct
	{
		int64_t tv_sec;
		int64_t tv_nsec;
	} st_atim;
	struct
	{
		int64_t tv_sec;
		int64_t tv_nsec;
	} st_mtim;
	struct
	{
		int64_t tv_sec;
		int64_t tv_nsec;
	} st_ctim;
	int64_t __unused[3];
};

void test_stat_user_abi(void)
{
	TEST_BEGIN("stat_user_abi");
	ASSERT(sizeof(struct musl_x64_stat_abi) == 144U);
	TEST_END();
}
