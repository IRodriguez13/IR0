/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: supervise_path.h
 * Description: runsv supervise path stale-VFS purge (tier-1 runit)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/stat.h>
#include <ir0/types.h>

/* Clear stale MINIX node before open on supervise regular files (pid.new, lock, …). */
int ir0_supervise_prepare_open(const char *path, int ir0_flags);

/* Clear stale node before renameat on supervise regular paths. */
int ir0_supervise_prepare_rename(const char *path);

/*
 * Stat overlay: if @vfs_st is a wrong-type VFS node on a supervise FIFO path,
 * remove it so named_fifo_stat can succeed.
 */
int ir0_supervise_clear_stale_if_not_fifo(const char *path, const stat_t *vfs_st);
