/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: path.h
 * Description: IR0 kernel source/header file
 */

#ifndef _IR0_PATH_H
#define _IR0_PATH_H

#include <stddef.h>

// Normalize a path, handling ".." and "." components
// Returns the normalized path in dest
// dest must be at least as large as src
// Returns 0 on success, -1 on error
int normalize_path(const char *src, char *dest, size_t size);

// Join two paths together, handling ".." and "." components
// Returns the joined path in dest
// dest must be large enough to hold both paths plus a separator
// Returns 0 on success, -1 on error
int join_paths(const char *base, const char *rel, char *dest, size_t size);

// Test if a path is absolute
int is_absolute_path(const char *path);

// Get parent directory path
// Returns the parent directory path in dest
// Returns 0 on success, -1 on error
int get_parent_path(const char *path, char *dest, size_t size);

#endif /* _IR0_PATH_H */