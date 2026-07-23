/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_log_hostshare.h
 * Description: Optional dump of boot log ring buffer to virtio-9p host share.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#define IR0_BOOT_LOG_HOSTSHARE_NAME "ir0-boot.log"

/*
 * If CONFIG_BOOT_LOG_HOSTSHARE=y and virtio-9p is ready, write the logging
 * ring buffer to IR0_BOOT_LOG_HOSTSHARE_NAME on the host share.
 * Returns 0 on success, 1 if skipped (config off / no 9p), <0 on I/O error.
 * Never panics — normal boots without -virtfs are fine.
 */
int ir0_boot_log_hostshare_try(void);
