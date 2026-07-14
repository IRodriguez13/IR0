/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rootfs_early.h
 * Description: Freestanding fake root for "/" and "/init" (no vfs_init_root).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/** Install fake nodes and print ARM64_ROOTFS_OK. Idempotent. */
void arm64_rootfs_early_init(void);

int arm64_rootfs_ready(void);

int64_t arm64_rootfs_openat(int dirfd, uint64_t path, int flags);
int64_t arm64_rootfs_read(int fd, uint64_t buf, uint64_t count);
int64_t arm64_rootfs_close(int fd);
int64_t arm64_rootfs_faccessat(int dirfd, uint64_t path, int flags);
int64_t arm64_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags);
int64_t arm64_rootfs_fstat(int fd, uint64_t statbuf);

/** EL1 smoke: open/read/fstat /init via fake FS. Returns 0 on OK. */
int arm64_rootfs_smoke_init(void);
