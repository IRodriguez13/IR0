/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: heartfs.h
 * Description: /heart — IR0 unified pseudo-fs facade (does not replace /proc|/sys)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/vfs.h>
#include <stddef.h>

int is_heart_path(const char *path);
int heart_is_virtual_subdir(const char *path);

/* Map /heart/proc/X → /proc/X and /heart/sys/X → /sys/X; else 0. */
int heart_alias_canonical(const char *path, char *out, size_t out_sz);

int heart_stat(const char *path, stat_t *st);
int heart_getdents(const char *path, struct vfs_dirent *entries, int max_entries);

void heart_nodes_register(void);
