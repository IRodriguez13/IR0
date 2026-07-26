/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: flock.h
 * Description: Advisory whole-file locks (flock(2)) keyed by open file.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

struct vfs_file;

/*
 * Apply LOCK_SH / LOCK_EX / LOCK_UN (optionally | LOCK_NB) to the open file
 * description @file. Returns 0, or -errno. A conflicting request returns
 * -EWOULDBLOCK with LOCK_NB and -ENOLCK without it: IR0 cannot yet park a task
 * on a lock queue, so it reports the failure instead of granting the lock.
 */
int ir0_flock_apply(struct vfs_file *file, int operation);

/* Drop any lock held by @file (called when the description is destroyed). */
void ir0_flock_release_file(struct vfs_file *file);
