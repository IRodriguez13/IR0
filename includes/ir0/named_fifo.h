/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: named_fifo.h
 * Description: In-memory named FIFO nodes (mknod S_IFIFO tier-1 subset)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/pipe.h>

int named_fifo_is_runsv_supervise_path(const char *path);
int named_fifo_is_runsv_supervise_regular_path(const char *path);
int named_fifo_create(const char *path, mode_t mode);
int named_fifo_stat(const char *path, stat_t *buf);
int named_fifo_unlink(const char *path);
pipe_t *named_fifo_lookup(const char *path);
