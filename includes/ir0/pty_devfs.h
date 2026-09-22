/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pty_devfs.h
 * Description: PTY multiplex facade (/dev/ptmx + /dev/pts/N) for syscall layer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Linux UNIX98 PTY multiplex (man ptmx, TIOCGPTN, TIOCSPTLCK).
 * Implementation: fs/devfs.c — portable code uses this facade only.
 */
#define DEVFS_PTMX_DEVICE_ID  43u
/*
 * Reserved devfs internal handle block for UNIX98 PTY slaves (Linux devpts
 * uses dynamic nodes; IR0 uses fixed pts/0..N-1). Must not overlap static
 * nodes in fs/devfs.c — enforced by scripts/architecture_guard.py.
 *
 * Static map (fs/devfs.c): 1-17 builtins, 40-47 stdio/tty/serial/event0,
 * 20-23 disk wholes; pts use 48..55; /dev/ktm uses DEVFS_KTM_DEVICE_ID (56).
 */
#define DEVFS_PTS0_DEVICE_ID  48u
#define DEVFS_PTY_MAX           8
#define DEVFS_PTY_ID_END        (DEVFS_PTS0_DEVICE_ID + DEVFS_PTY_MAX)

static inline int devfs_is_ptmx_device(uint32_t device_id)
{
	return device_id == DEVFS_PTMX_DEVICE_ID;
}

static inline int devfs_is_pts_device(uint32_t device_id)
{
	return device_id >= DEVFS_PTS0_DEVICE_ID &&
	       device_id < DEVFS_PTS0_DEVICE_ID + DEVFS_PTY_MAX;
}

int devfs_pty_take_pending_master_slot(void);
void devfs_pty_abort_pending_master(void);
void *devfs_pty_vfs_mark(int slot);
int devfs_pty_vfs_slot(const void *vfs_file);
void devfs_pty_master_release_vfs(const void *vfs_file);
void devfs_pty_master_dup_vfs(const void *vfs_file);
void devfs_pty_slave_dup_device(uint32_t device_id);
int64_t devfs_pty_master_read(const void *vfs_file, void *buf, size_t count);
int64_t devfs_pty_master_write(const void *vfs_file, const void *buf,
			       size_t count);
int64_t devfs_pty_master_ioctl(const void *vfs_file, uint64_t request,
			       void *arg);
int devfs_pty_master_can_read(const void *vfs_file);
int devfs_pty_master_can_write(const void *vfs_file);
