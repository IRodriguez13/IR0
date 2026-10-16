/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_rootfs.h
 * Description: Staged "/", "/init", and Linux /dev char nodes (ISA supplies blob/usercopy)
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
	IR0_EARLY_ROOTFS_DEV,
	IR0_EARLY_ROOTFS_CONSOLE,
	IR0_EARLY_ROOTFS_NULL,
	IR0_EARLY_ROOTFS_ZERO,
	IR0_EARLY_ROOTFS_TTY,
};

#define IR0_EARLY_ROOTFS_S_IFMT  0170000U
#define IR0_EARLY_ROOTFS_S_IFREG 0100000U
#define IR0_EARLY_ROOTFS_S_IFDIR 0040000U
#define IR0_EARLY_ROOTFS_S_IFCHR 0020000U
#define IR0_EARLY_ROOTFS_MODE_DIR \
	(IR0_EARLY_ROOTFS_S_IFDIR | 0755U)
#define IR0_EARLY_ROOTFS_MODE_REG \
	(IR0_EARLY_ROOTFS_S_IFREG | 0755U)
#define IR0_EARLY_ROOTFS_MODE_CHR \
	(IR0_EARLY_ROOTFS_S_IFCHR | 0622U)
/*
 * Linux devices.txt (docs.kernel.org/admin-guide/devices.html):
 *   /dev/null    char 1:3
 *   /dev/zero    char 1:5
 *   /dev/tty     char 5:0
 *   /dev/console char 5:1
 * GNU makedev(major, minor) for these 8-bit pairs is (major << 8) | minor.
 */
#define IR0_EARLY_ROOTFS_NULL_RDEV    0x103ULL
#define IR0_EARLY_ROOTFS_ZERO_RDEV    0x105ULL
#define IR0_EARLY_ROOTFS_TTY_RDEV     0x500ULL
#define IR0_EARLY_ROOTFS_CONSOLE_RDEV 0x501ULL

static inline int ir0_early_rootfs_path_eq(const char *path, const char *lit)
{
	if (!path || !lit)
		return 0;
	while (*path && *path == *lit)
	{
		path++;
		lit++;
	}
	return *path == *lit;
}

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
	if (ir0_early_rootfs_path_eq(path, "/"))
		return IR0_EARLY_ROOTFS_ROOT;
	if (ir0_early_rootfs_path_eq(path, "/init"))
		return IR0_EARLY_ROOTFS_INIT;
	if (ir0_early_rootfs_path_eq(path, "/dev"))
		return IR0_EARLY_ROOTFS_DEV;
	if (ir0_early_rootfs_path_eq(path, "/dev/console"))
		return IR0_EARLY_ROOTFS_CONSOLE;
	if (ir0_early_rootfs_path_eq(path, "/dev/null"))
		return IR0_EARLY_ROOTFS_NULL;
	if (ir0_early_rootfs_path_eq(path, "/dev/zero"))
		return IR0_EARLY_ROOTFS_ZERO;
	if (ir0_early_rootfs_path_eq(path, "/dev/tty"))
		return IR0_EARLY_ROOTFS_TTY;
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
	if (node == IR0_EARLY_ROOTFS_CONSOLE ||
	    node == IR0_EARLY_ROOTFS_NULL ||
	    node == IR0_EARLY_ROOTFS_ZERO ||
	    node == IR0_EARLY_ROOTFS_TTY)
	{
		st->st_mode = IR0_EARLY_ROOTFS_MODE_CHR;
		st->st_size = 0;
		st->st_blocks = 0;
		if (node == IR0_EARLY_ROOTFS_CONSOLE)
		{
			st->st_ino = 4;
			st->st_rdev = IR0_EARLY_ROOTFS_CONSOLE_RDEV;
			return;
		}
		if (node == IR0_EARLY_ROOTFS_NULL)
		{
			st->st_ino = 5;
			st->st_rdev = IR0_EARLY_ROOTFS_NULL_RDEV;
			return;
		}
		if (node == IR0_EARLY_ROOTFS_ZERO)
		{
			st->st_ino = 6;
			st->st_rdev = IR0_EARLY_ROOTFS_ZERO_RDEV;
			return;
		}
		st->st_ino = 7;
		st->st_rdev = IR0_EARLY_ROOTFS_TTY_RDEV;
		return;
	}
	if (node == IR0_EARLY_ROOTFS_DEV)
	{
		st->st_ino = 3;
		st->st_mode = IR0_EARLY_ROOTFS_MODE_DIR;
		st->st_size = 0;
		st->st_blocks = 0;
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
int early_rootfs_console_ready(void);
int early_rootfs_memdev_ready(void);
int early_rootfs_smoke_init(void);
int early_rootfs_fd_is_console(int fd);
int early_rootfs_fd_discards_write(int fd);
int64_t early_rootfs_dup(int oldfd);
int64_t early_rootfs_dup3(int oldfd, int newfd, int flags);
int64_t early_rootfs_chdir(uint64_t path);
int64_t early_rootfs_getcwd(uint64_t buffer, uint64_t size);

int64_t early_rootfs_openat(int dirfd, uint64_t path, int flags);
int64_t early_rootfs_read(int fd, uint64_t buffer, uint64_t count);
int64_t early_rootfs_close(int fd);
int64_t early_rootfs_faccessat(int dirfd, uint64_t path, int flags);
int64_t early_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags);
int64_t early_rootfs_fstat(int fd, uint64_t statbuf);
int64_t early_rootfs_readlinkat(int dirfd, uint64_t path, uint64_t buffer,
				uint64_t size);
