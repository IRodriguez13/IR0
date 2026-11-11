/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * Linux arch/x86/include/uapi/asm/stat.h struct stat (x86_64): st_nlink is
 * 64-bit and comes before st_mode. Selected by includes/ir0/stat_user.h.
 */
#include <stdint.h>

struct ir0_user_stat
{
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;

	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	uint32_t __pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;

	struct
	{
		int64_t tv_sec;
		int64_t tv_nsec;
	} st_atim, st_mtim, st_ctim;
	int64_t __unused[3];
};
