/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: product_vfs.c
 * Description: CHDIR / OPENAT / STAT via product path + staged fallback
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/copy_user.h>
#include <ir0/cwd.h>
#include <ir0/early_rootfs.h>
#include <ir0/errno.h>
#include <ir0/product_path.h>
#include <ir0/product_vfs.h>
#include <ir0/stat.h>
#include <ir0/syscall_id.h>

static int product_copy_path(uint64_t src, char *dst, unsigned cap)
{
	unsigned i;

	if (!src || !dst || cap < 2)
		return -EFAULT;
	for (i = 0; i < cap; i++)
	{
		if (copy_from_user(dst + i, (const void *)(uintptr_t)(src + i),
				   1) != 0)
			return -EFAULT;
		if (dst[i] == '\0')
			return 0;
	}
	return -ENAMETOOLONG;
}

static int64_t product_chdir(uint64_t path)
{
	char pbuf[256];
	stat_t st;
	const struct ir0_product_path_ops *ops;
	const struct ir0_cwd_ops *cwd;
	int64_t ret;
	int rc;

	rc = product_copy_path(path, pbuf, sizeof(pbuf));
	if (rc < 0)
		return rc;
	if (pbuf[0] == '\0')
		return -ENOENT;
	ops = ir0_product_path_bound();
	if (ops && ops->stat && ir0_product_path_is_tmp(pbuf))
	{
		if (ops->stat(pbuf, &st) < 0)
			return -ENOENT;
		if (!S_ISDIR(st.st_mode))
			return -ENOTDIR;
		cwd = ir0_cwd_product();
		if (cwd && cwd->set)
			cwd->set(pbuf);
		ret = early_rootfs_chdir(path);
		if (ret == -ENOENT)
			return 0;
		return ret;
	}
	return early_rootfs_chdir(path);
}

static int64_t product_vfs_syscall(void *context, enum ir0_syscall_id id,
				   uint64_t a0, uint64_t a1, uint64_t a2,
				   uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)context;
	(void)a4;
	(void)a5;
	if (id == IR0_SYSCALL_CHDIR)
		return product_chdir(a0);
	if (id == IR0_SYSCALL_OPENAT)
		return early_rootfs_openat((int)a0, a1, (int)a2);
	if (id == IR0_SYSCALL_NEWFSTATAT)
		return early_rootfs_newfstatat((int)a0, a1, a2, (int)a3);
	if (id == IR0_SYSCALL_FSTAT)
		return early_rootfs_fstat((int)a0, a1);
	return -ENOSYS;
}

static const enum ir0_syscall_id g_product_vfs_ids[] = {
	IR0_SYSCALL_CHDIR, IR0_SYSCALL_OPENAT, IR0_SYSCALL_NEWFSTATAT,
	IR0_SYSCALL_FSTAT,
};

const struct syscall_context_provider ir0_product_vfs_provider = {
	.ids = g_product_vfs_ids,
	.count = sizeof(g_product_vfs_ids) / sizeof(g_product_vfs_ids[0]),
	.handler = product_vfs_syscall,
};
