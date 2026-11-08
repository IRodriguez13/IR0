/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: swap_syscalls.c
 * Description: swapon(2) / swapoff(2) syscall entry points.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mm_syscalls.h"
#include <ir0/vfs.h>
#include <ir0/process.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/blockdev.h>
#include <ir0/vfs_dispatch.h>
#include <string.h>

static int swap_header_from_lba(dev_t dev, uint64_t lba,
				struct ir0_swap_header_info *info)
{
	unsigned char page[IR0_SWAP_PAGE_SIZE];
	int rc;

	rc = ir0_block_read(dev, lba, 8, page);
	if (rc < 0)
		return rc;
	return ir0_swap_parse_header(page, sizeof(page), info);
}

static const char *swap_block_name(const char *path)
{
	if (strncmp(path, "/dev/", 5) == 0)
		return path + 5;
	return path;
}

int64_t sys_swapon(const char *user_path, int flags)
{
	char resolved[256];
	stat_t st;
	struct ir0_swap_header_info hdr;
	struct ir0_swap_extent extents[IR0_SWAP_MAX_EXTENTS];
	size_t nextent = 0;
	dev_t dev;
	int kind;
	int rc;

	if (!current_process || current_process->euid != ROOT_UID)
		return -EPERM;
	if (!user_path)
		return -EFAULT;
	rc = ir0_resolve_user_path(user_path, resolved, sizeof(resolved),
				   current_process->cwd, current_process->root);
	if (rc < 0)
		return rc;
	if (vfs_stat(resolved, &st) != 0)
		return -ENOENT;
	if (S_ISBLK(st.st_mode))
	{
		struct ir0_block_info binfo;
		uint64_t device_pages;

		dev = ir0_block_lookup_by_name(swap_block_name(resolved));
		if (!dev)
			return -ENODEV;
		if (ir0_block_get_info(dev, &binfo) != 0)
			return -ENODEV;
		rc = swap_header_from_lba(dev, 0, &hdr);
		if (rc < 0)
			return rc;
		device_pages = binfo.sector_count / 8ULL;
		if (device_pages <= hdr.last_page)
			return -EINVAL;
		extents[0].file_page = 0;
		extents[0].phys_lba = 0;
		extents[0].nr_pages = hdr.last_page + 1U;
		nextent = 1;
		kind = IR0_SWAP_KIND_BLOCK;
	}
	else if (S_ISREG(st.st_mode))
	{
		struct vfs_ops *ops = vfs_dispatch_ops(resolved);

		if (!ops || !ops->map_swap_extents)
			return -EOPNOTSUPP;
		rc = ops->map_swap_extents(resolved, extents, IR0_SWAP_MAX_EXTENTS,
					   &nextent, &dev);
		if (rc < 0)
			return rc;
		if (!dev || nextent == 0 || extents[0].file_page != 0)
			return -EINVAL;
		rc = swap_header_from_lba(dev, extents[0].phys_lba, &hdr);
		if (rc < 0)
			return rc;
		kind = IR0_SWAP_KIND_FILE;
	}
	else
		return -EINVAL;

	return ir0_swap_area_install(resolved, kind, flags, dev, extents, nextent,
				     hdr.last_page);
}

int64_t sys_swapoff(const char *user_path)
{
	char resolved[256];
	int rc;

	if (!current_process || current_process->euid != ROOT_UID)
		return -EPERM;
	if (!user_path)
		return -EFAULT;
	rc = ir0_resolve_user_path(user_path, resolved, sizeof(resolved),
				   current_process->cwd, current_process->root);
	if (rc < 0)
		return rc;
	return ir0_swap_area_remove(resolved);
}
