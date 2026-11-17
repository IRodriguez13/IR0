/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: filesystems.c
 * Description: The table of filesystem types built into the kernel.
 *
 * Each filesystem defines one struct vfs_fstype in its own directory and
 * registers nothing itself. The build lists the selected types in
 * <generated/filesystems.h> as IR0_FILESYSTEMS(fs), one fs(type) per
 * filesystem .config enables (scripts/make/fs.mk); vfs_init() registers
 * them in that order. The VFS never names a filesystem.
 */

#include "vfs.h"
#include "vfs_internal.h"
#include <generated/filesystems.h>
#include <stddef.h>

#define FS_DECLARE(type) extern struct vfs_fstype type;
IR0_FILESYSTEMS(FS_DECLARE)

#define FS_ENTRY(type) &type,
struct vfs_fstype *const vfs_builtin_filesystems[] = {
	IR0_FILESYSTEMS(FS_ENTRY)
	NULL,
};
