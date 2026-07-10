/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ext2_disk.h
 * Description: Minimal read-only EXT2 on ir0 block devices.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

int ext2_fs_register(void);
int ext2_disk_mount(const char *dev, const char *mount_dir);
