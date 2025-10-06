// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.c
 * Description: Virtual File System abstraction layer with MINIX filesystem integration
 */

#include "vfs.h"
#include "minix_fs.h"
#include <string.h>

// External memory functions
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Lista de filesystems registrados
static struct filesystem_type *filesystems = NULL;

// Root filesystem
static struct vfs_superblock *root_sb = NULL;
static struct vfs_inode *root_inode = NULL;
