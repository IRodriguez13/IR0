/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rootfs_early.c
 * Description: Minimal fake FS — "/" directory and "/init" (embedded BusyBox blob).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "rootfs_early.h"
#include "mmu_early.h"
#include "pl011.h"

#include <stdint.h>
#include <ir0/boot_log.h>

#define AT_FDCWD (-100)
#define EBADF   9
#define EFAULT  14
#define EISDIR  21
#define ENOENT  2
#define EMFILE  24

#define S_IFMT   0170000U
#define S_IFREG  0100000U
#define S_IFDIR  0040000U
#define S_IXUSR  0100U
#define S_IRUSR  0400U
#define S_IWUSR  0200U
#define S_IXGRP  0010U
#define S_IRGRP  0040U
#define S_IXOTH  0001U
#define S_IROTH  0004U

#define ROOTFS_MODE_DIR  (S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | \
			  S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define ROOTFS_MODE_REG  (S_IFREG | S_IRUSR | S_IWUSR | S_IXUSR | \
			  S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)

#define ROOTFS_MAX_OPEN 8U
#define ROOTFS_FD_BASE  3

enum rootfs_node
{
	ROOTFS_NODE_NONE = 0,
	ROOTFS_NODE_ROOT,
	ROOTFS_NODE_INIT,
};

struct rootfs_slot
{
	int used;
	enum rootfs_node node;
	uint64_t pos;
};

struct linux_stat64
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

extern const uint8_t busybox_aarch64_blob[];
extern const uint8_t busybox_aarch64_blob_end[];

static struct rootfs_slot g_slots[ROOTFS_MAX_OPEN];
static int g_rootfs_ready;

static uint64_t init_blob_len(void)
{
	return (uint64_t)(busybox_aarch64_blob_end - busybox_aarch64_blob);
}

static int copy_user_cstr(uint64_t upath, char *buf, unsigned bufsz)
{
	const volatile uint8_t *src;
	unsigned i;

	if (!buf || bufsz == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok(upath, 1))
		return 0;

	src = (const volatile uint8_t *)(uintptr_t)upath;
	for (i = 0; i < bufsz - 1; i++)
	{
		buf[i] = (char)src[i];
		if (src[i] == 0)
			return 1;
	}
	return 0;
}

static int copy_user_bytes(uint64_t uaddr, void *dst, uint64_t n)
{
	volatile uint8_t *d = (volatile uint8_t *)dst;
	const volatile uint8_t *src;
	uint64_t i;

	if (!dst || n == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok(uaddr, n))
		return 0;

	src = (const volatile uint8_t *)(uintptr_t)uaddr;
	for (i = 0; i < n; i++)
		d[i] = src[i];
	return 1;
}

static int store_user_bytes(uint64_t uaddr, const void *src, uint64_t n)
{
	volatile uint8_t *dst;
	const volatile uint8_t *s;
	uint64_t i;

	if (!src || n == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok(uaddr, n))
		return 0;

	dst = (volatile uint8_t *)(uintptr_t)uaddr;
	s = (const volatile uint8_t *)src;
	for (i = 0; i < n; i++)
		dst[i] = s[i];
	return 1;
}

static enum rootfs_node path_to_node(const char *path)
{
	if (!path)
		return ROOTFS_NODE_NONE;
	if (path[0] == '/' && path[1] == 'i' && path[2] == 'n' &&
	    path[3] == 'i' && path[4] == 't' && path[5] == 0)
		return ROOTFS_NODE_INIT;
	if (path[0] == '/' && path[1] == 0)
		return ROOTFS_NODE_ROOT;
	return ROOTFS_NODE_NONE;
}

static int slot_from_fd(int fd)
{
	unsigned idx;

	if (fd < ROOTFS_FD_BASE)
		return -1;
	idx = (unsigned)(fd - ROOTFS_FD_BASE);
	if (idx >= ROOTFS_MAX_OPEN || !g_slots[idx].used)
		return -1;
	return (int)idx;
}

static int alloc_fd(enum rootfs_node node)
{
	unsigned i;

	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		if (!g_slots[i].used)
		{
			g_slots[i].used = 1;
			g_slots[i].node = node;
			g_slots[i].pos = 0;
			return (int)(ROOTFS_FD_BASE + i);
		}
	}
	return -EMFILE;
}

static void fill_stat(enum rootfs_node node, struct linux_stat64 *st)
{
	uint64_t blob_len = init_blob_len();

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

	if (node == ROOTFS_NODE_INIT)
	{
		st->st_ino = 2;
		st->st_mode = ROOTFS_MODE_REG;
		st->st_size = (int64_t)blob_len;
		st->st_blocks = (int64_t)((blob_len + 511UL) / 512UL);
	}
	else
	{
		st->st_ino = 1;
		st->st_mode = ROOTFS_MODE_DIR;
		st->st_size = 0;
		st->st_blocks = 0;
	}
}

void arm64_rootfs_early_init(void)
{
	unsigned i;

	if (g_rootfs_ready)
		return;

	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		g_slots[i].used = 0;
		g_slots[i].node = ROOTFS_NODE_NONE;
		g_slots[i].pos = 0;
	}
	g_rootfs_ready = 1;
	ir0_boot_smoke("ARM64_ROOTFS_OK");
}

int arm64_rootfs_ready(void)
{
	return g_rootfs_ready;
}

int64_t arm64_rootfs_openat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];
	enum rootfs_node node;
	int fd;

	(void)dirfd;
	(void)flags;

	if (!g_rootfs_ready)
		return -ENOENT;
	if (dirfd != AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;

	node = path_to_node(pbuf);
	if (node == ROOTFS_NODE_NONE)
		return -ENOENT;

	fd = alloc_fd(node);
	if (fd < 0)
		return fd;
	return fd;
}

int64_t arm64_rootfs_read(int fd, uint64_t buf, uint64_t count)
{
	int idx;
	struct rootfs_slot *sl;
	uint64_t blob_len;
	uint64_t remain;
	uint64_t n;
	uint8_t tmp[256];

	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	if (count == 0)
		return 0;

	sl = &g_slots[idx];
	if (sl->node == ROOTFS_NODE_ROOT)
		return -EISDIR;

	blob_len = init_blob_len();
	if (sl->pos >= blob_len)
		return 0;

	remain = blob_len - sl->pos;
	n = count > remain ? remain : count;
	if (n > sizeof(tmp))
		n = sizeof(tmp);

	if (!store_user_bytes(buf, busybox_aarch64_blob + sl->pos, n))
		return -EFAULT;

	sl->pos += n;
	return (int64_t)n;
}

int64_t arm64_rootfs_close(int fd)
{
	int idx;

	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;

	g_slots[idx].used = 0;
	g_slots[idx].node = ROOTFS_NODE_NONE;
	g_slots[idx].pos = 0;
	return 0;
}

int64_t arm64_rootfs_faccessat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];

	(void)dirfd;
	(void)flags;

	if (!g_rootfs_ready)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (path_to_node(pbuf) == ROOTFS_NODE_NONE)
		return -ENOENT;
	return 0;
}

int64_t arm64_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags)
{
	char pbuf[32];
	enum rootfs_node node;
	struct linux_stat64 st;

	(void)dirfd;
	(void)flags;

	if (!g_rootfs_ready)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;

	node = path_to_node(pbuf);
	if (node == ROOTFS_NODE_NONE)
		return -ENOENT;
	if (!arm64_mmu_user_buf_ok(statbuf, sizeof(st)))
		return -EFAULT;

	fill_stat(node, &st);
	if (!store_user_bytes(statbuf, &st, sizeof(st)))
		return -EFAULT;
	return 0;
}

int64_t arm64_rootfs_fstat(int fd, uint64_t statbuf)
{
	int idx;
	struct linux_stat64 st;

	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	if (!arm64_mmu_user_buf_ok(statbuf, sizeof(st)))
		return -EFAULT;

	fill_stat(g_slots[idx].node, &st);
	if (!store_user_bytes(statbuf, &st, sizeof(st)))
		return -EFAULT;
	return 0;
}

static int open_node(enum rootfs_node node)
{
	return alloc_fd(node);
}

int arm64_rootfs_smoke_init(void)
{
	int fd;
	struct linux_stat64 st;
	uint8_t magic[4];

	if (!g_rootfs_ready)
		return -1;

	fd = open_node(ROOTFS_NODE_INIT);
	if (fd < 0)
		return -1;

	fill_stat(ROOTFS_NODE_INIT, &st);
	if (st.st_mode != ROOTFS_MODE_REG || st.st_size <= 0)
	{
		arm64_rootfs_close(fd);
		return -1;
	}

	magic[0] = busybox_aarch64_blob[0];
	magic[1] = busybox_aarch64_blob[1];
	magic[2] = busybox_aarch64_blob[2];
	magic[3] = busybox_aarch64_blob[3];
	if (magic[0] != 0x7f || magic[1] != 'E' || magic[2] != 'L' ||
	    magic[3] != 'F')
	{
		arm64_rootfs_close(fd);
		return -1;
	}

	arm64_rootfs_close(fd);
	return 0;
}
