/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mount_prefix.c
 * Description: Longest-prefix mount path matching for block filesystem backends.
 */

#include <ir0/mount_prefix.h>

int ir0_mount_prefix_boundary_ok(const char *path, const char *mount_path,
				 size_t mount_len)
{
	if (!path || !mount_path || mount_len == 0)
		return 0;
	if (path[mount_len] == '\0' || path[mount_len] == '/')
		return 1;
	if (mount_len == 1 && mount_path[0] == '/')
		return 1;
	return 0;
}
