/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_stat_user_abi.c
 * Description: Host test — per-ISA Linux struct stat layouts for syscall copies
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <stddef.h>
#include <stdint.h>

/*
 * Both ISA layouts behind includes/ir0/stat_user.h, renamed so one host
 * object can compare them with the Linux uapi offsets.
 */
#define ir0_user_stat ir0_user_stat_x86_64
#include "../../arch/x86-64/include/asm/stat.h"
#undef ir0_user_stat
#define ir0_user_stat ir0_user_stat_arm64
#include "../../arch/arm64/include/asm/stat.h"
#undef ir0_user_stat

void test_stat_user_abi(void)
{
	TEST_BEGIN("stat_user_abi");
	/* arch/x86/include/uapi/asm/stat.h */
	ASSERT(sizeof(struct ir0_user_stat_x86_64) == 144U);
	ASSERT(offsetof(struct ir0_user_stat_x86_64, st_nlink) == 16U);
	ASSERT(offsetof(struct ir0_user_stat_x86_64, st_mode) == 24U);
	ASSERT(offsetof(struct ir0_user_stat_x86_64, st_size) == 48U);
	ASSERT(offsetof(struct ir0_user_stat_x86_64, st_atim) == 72U);
	/* include/uapi/asm-generic/stat.h (arm64) */
	ASSERT(sizeof(struct ir0_user_stat_arm64) == 128U);
	ASSERT(offsetof(struct ir0_user_stat_arm64, st_mode) == 16U);
	ASSERT(offsetof(struct ir0_user_stat_arm64, st_nlink) == 20U);
	ASSERT(offsetof(struct ir0_user_stat_arm64, st_size) == 48U);
	ASSERT(offsetof(struct ir0_user_stat_arm64, st_blksize) == 56U);
	ASSERT(offsetof(struct ir0_user_stat_arm64, st_blocks) == 64U);
	ASSERT(offsetof(struct ir0_user_stat_arm64, st_atim) == 72U);
	TEST_END();
}
