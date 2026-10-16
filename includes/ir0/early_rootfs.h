/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_rootfs.h
 * Description: Staged "/" + "/init" nodes (ISA supplies blob and usercopy)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/early_syscall.h>

#define IR0_EARLY_ROOTFS_AT_FDCWD (-100)

enum ir0_early_rootfs_node
{
	IR0_EARLY_ROOTFS_NONE = 0,
	IR0_EARLY_ROOTFS_ROOT,
	IR0_EARLY_ROOTFS_INIT,
};

#define IR0_EARLY_ROOTFS_S_IFMT  0170000U
#define IR0_EARLY_ROOTFS_S_IFREG 0100000U
#define IR0_EARLY_ROOTFS_S_IFDIR 0040000U
#define IR0_EARLY_ROOTFS_MODE_DIR \
	(IR0_EARLY_ROOTFS_S_IFDIR | 0755U)
#define IR0_EARLY_ROOTFS_MODE_REG \
	(IR0_EARLY_ROOTFS_S_IFREG | 0755U)

struct ir0_early_stat64
{
	uint64_t st_dev;
	uint64_t st_ino;
	uint32_t st_mode;
	uint32_t st_nlink;
	uint64_t st_uid;
	uint64_t st_gid;
	uint64_t st_rdev;
	uint64_t __pad1;
	int64_t st_size;
	int32_t st_blksize;
	int32_t __pad2;
	int64_t st_blocks;
	int64_t st_atime_sec;
	uint64_t st_atime_nsec;
	int64_t st_mtime_sec;
	uint64_t st_mtime_nsec;
	int64_t st_ctime_sec;
	uint64_t st_ctime_nsec;
	uint64_t __unused[2];
};

static inline enum ir0_early_rootfs_node ir0_early_rootfs_path_node(const char *path)
{
	if (!path)
		return IR0_EARLY_ROOTFS_NONE;
	if (path[0] == '/' && path[1] == '\0')
		return IR0_EARLY_ROOTFS_ROOT;
	if (path[0] == '/' && path[1] == 'i' && path[2] == 'n' &&
	    path[3] == 'i' && path[4] == 't' && path[5] == '\0')
		return IR0_EARLY_ROOTFS_INIT;
	return IR0_EARLY_ROOTFS_NONE;
}

static inline void ir0_early_rootfs_fill_stat(enum ir0_early_rootfs_node node,
					      uint64_t blob_len,
					      struct ir0_early_stat64 *st)
{
	if (!st)
		return;

	st->st_dev = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_nlink = 1;
	st->st_blksize = 4096;
	st->st_atime_sec = 0;
	st->st_atime_nsec = 0;
	st->st_mtime_sec = 0;
	st->st_mtime_nsec = 0;
	st->st_ctime_sec = 0;
	st->st_ctime_nsec = 0;
	st->__pad1 = 0;
	st->__pad2 = 0;
	st->__unused[0] = 0;
	st->__unused[1] = 0;

	if (node == IR0_EARLY_ROOTFS_INIT)
	{
		st->st_ino = 2;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_REG;
		st->st_size = (int64_t)blob_len;
		st->st_blocks = (int64_t)((blob_len + 511UL) / 512UL);
		return;
	}

	st->st_ino = 1;
	st->st_mode = IR0_EARLY_ROOTFS_MODE_DIR;
	st->st_size = 0;
	st->st_blocks = 0;
}

void early_rootfs_bind(const uint8_t *init_blob, uint64_t init_len,
		       const struct early_user_services *user);
int early_rootfs_ready(void);
int early_rootfs_smoke_init(void);

int64_t early_rootfs_openat(int dirfd, uint64_t path, int flags);
int64_t early_rootfs_read(int fd, uint64_t buffer, uint64_t count);
int64_t early_rootfs_close(int fd);
int64_t early_rootfs_faccessat(int dirfd, uint64_t path, int flags);
int64_t early_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags);
int64_t early_rootfs_fstat(int fd, uint64_t statbuf);
int64_t early_rootfs_readlinkat(int dirfd, uint64_t path, uint64_t buffer,
				uint64_t size);
