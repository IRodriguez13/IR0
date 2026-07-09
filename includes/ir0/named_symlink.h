/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: named_symlink.h
 * Description: In-memory symbolic links for runsv supervise paths (tier-1)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/stat.h>
#include <ir0/types.h>
#include <stddef.h>

#define IR0_SYMLINK_FOLLOW_MAX 8

int named_symlink_create(const char *linkpath, const char *target);
int named_symlink_stat(const char *path, stat_t *buf);
const char *named_symlink_target(const char *path);
int named_symlink_unlink(const char *path);
int ir0_follow_named_symlinks(char *path, size_t path_sz, unsigned max_depth);
