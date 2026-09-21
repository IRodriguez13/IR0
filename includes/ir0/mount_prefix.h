/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mount_prefix.h
 * Description: Longest-prefix mount path matching for block filesystem backends.
 */

#pragma once

#include <stddef.h>

/*
 * Return 1 when @path is under @mount_path (prefix match with valid boundary).
 * @mount_len is strlen(mount_path); caller must already verify strncmp prefix.
 *
 * Root mount "/" matches every absolute path (STO-2 ext2 / regression).
 * Non-root mounts require exact path, or next char is '/'.
 */
int ir0_mount_prefix_boundary_ok(const char *path, const char *mount_path,
			       size_t mount_len);
