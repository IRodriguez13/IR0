/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: early_rootfs.c
 * Description: Architecture-neutral staged "/" and "/init" file table
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <ir0/early_rootfs.h>

#define EBADF  9
#define EFAULT 14
#define EISDIR 21
#define ENOENT 2
#define EMFILE 24

#define ROOTFS_MAX_OPEN 8U
#define ROOTFS_FD_BASE  3

struct rootfs_slot
{
	int used;
	enum ir0_early_rootfs_node node;
	uint64_t pos;
};

static struct rootfs_slot g_slots[ROOTFS_MAX_OPEN];
static const uint8_t *g_blob;
static uint64_t g_blob_len;
static const struct early_user_services *g_user;
static int g_ready;

static int copy_user_cstr(uint64_t upath, char *buf, unsigned bufsz)
{
	unsigned i;

	if (!buf || bufsz == 0 || !g_user || !g_user->copy_from_user)
		return 0;
	for (i = 0; i < bufsz - 1; i++)
	{
		if (g_user->copy_from_user(&buf[i], upath + i, 1) != 0)
			return 0;
		if (buf[i] == '\0')
			return 1;
	}
	return 0;
}

static int strings_equal(const char *left, const char *right)
{
	if (!left || !right)
		return 0;
	while (*left && *left == *right)
	{
		left++;
		right++;
	}
	return *left == *right;
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

static int alloc_fd(enum ir0_early_rootfs_node node)
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

void early_rootfs_bind(const uint8_t *init_blob, uint64_t init_len,
		       const struct early_user_services *user)
{
	unsigned i;

	g_blob = init_blob;
	g_blob_len = init_len;
	g_user = user;
	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		g_slots[i].used = 0;
		g_slots[i].node = IR0_EARLY_ROOTFS_NONE;
		g_slots[i].pos = 0;
	}
	g_ready = (init_blob != 0 && init_len > 0 && user &&
		   user->copy_from_user && user->copy_to_user);
}

int early_rootfs_ready(void)
{
	return g_ready;
}

int64_t early_rootfs_openat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	int fd;

	(void)flags;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = ir0_early_rootfs_path_node(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	fd = alloc_fd(node);
	if (fd < 0)
		return fd;
	return fd;
}

int64_t early_rootfs_read(int fd, uint64_t buffer, uint64_t count)
{
	int idx;
	struct rootfs_slot *sl;
	uint64_t remain;
	uint64_t n;
	uint8_t tmp[256];

	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	if (count == 0)
		return 0;
	sl = &g_slots[idx];
	if (sl->node == IR0_EARLY_ROOTFS_ROOT)
		return -EISDIR;
	if (!g_blob || sl->pos >= g_blob_len)
		return 0;
	remain = g_blob_len - sl->pos;
	n = count > remain ? remain : count;
	if (n > sizeof(tmp))
		n = sizeof(tmp);
	{
		uint64_t i;

		for (i = 0; i < n; i++)
			tmp[i] = g_blob[sl->pos + i];
	}
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, tmp, n) != 0)
		return -EFAULT;
	sl->pos += n;
	return (int64_t)n;
}

int64_t early_rootfs_close(int fd)
{
	int idx;

	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	g_slots[idx].used = 0;
	g_slots[idx].node = IR0_EARLY_ROOTFS_NONE;
	g_slots[idx].pos = 0;
	return 0;
}

int64_t early_rootfs_readlinkat(int dirfd, uint64_t path, uint64_t buffer,
				uint64_t size)
{
	static const char proc_self_exe[] = "/proc/self/exe";
	static const char init_target[] = "/init";
	char pbuf[32];
	uint64_t n = sizeof(init_target) - 1U;

	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (!strings_equal(pbuf, proc_self_exe))
		return -ENOENT;
	if (size < n)
		n = size;
	if (n > 0 && (!g_user || !g_user->copy_to_user ||
		      g_user->copy_to_user(buffer, init_target, n) != 0))
		return -EFAULT;
	return (int64_t)n;
}

int64_t early_rootfs_faccessat(int dirfd, uint64_t path, int flags)
{
	char pbuf[32];

	(void)flags;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	if (ir0_early_rootfs_path_node(pbuf) == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	return 0;
}

int64_t early_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;
	struct ir0_early_stat64 st;

	(void)flags;
	if (!g_ready)
		return -ENOENT;
	if (dirfd != IR0_EARLY_ROOTFS_AT_FDCWD && slot_from_fd(dirfd) < 0)
		return -EBADF;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = ir0_early_rootfs_path_node(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	ir0_early_rootfs_fill_stat(node, g_blob_len, &st);
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(statbuf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_fstat(int fd, uint64_t statbuf)
{
	int idx;
	struct ir0_early_stat64 st;

	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	ir0_early_rootfs_fill_stat(g_slots[idx].node, g_blob_len, &st);
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(statbuf, &st, sizeof(st)) != 0)
		return -EFAULT;
	return 0;
}

int early_rootfs_smoke_init(void)
{
	int fd;
	struct ir0_early_stat64 st;

	if (!g_ready || !g_blob || g_blob_len < 4)
		return -1;
	fd = alloc_fd(IR0_EARLY_ROOTFS_INIT);
	if (fd < 0)
		return -1;
	ir0_early_rootfs_fill_stat(IR0_EARLY_ROOTFS_INIT, g_blob_len, &st);
	if (st.st_mode != IR0_EARLY_ROOTFS_MODE_REG || st.st_size <= 0 ||
	    g_blob[0] != 0x7f || g_blob[1] != 'E' || g_blob[2] != 'L' ||
	    g_blob[3] != 'F')
	{
		early_rootfs_close(fd);
		return -1;
	}
	early_rootfs_close(fd);
	return 0;
}
