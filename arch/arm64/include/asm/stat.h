/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * Linux include/uapi/asm-generic/stat.h struct stat (arm64): st_mode and a
 * 32-bit st_nlink come right after st_ino, st_blksize is 32-bit.
 * Selected by includes/ir0/stat_user.h.
 */
#include <stdint.h>

struct ir0_user_stat
{
	uint64_t st_dev;
	uint64_t st_ino;
	uint32_t st_mode;
	uint32_t st_nlink;
	uint32_t st_uid;
	uint32_t st_gid;
	uint64_t st_rdev;
	uint64_t __pad1;
	int64_t st_size;
	int32_t st_blksize;
	int32_t __pad2;
	int64_t st_blocks;

	struct
	{
		int64_t tv_sec;
		int64_t tv_nsec;
	} st_atim, st_mtim, st_ctim;
	uint32_t __unused4;
	uint32_t __unused5;
};
