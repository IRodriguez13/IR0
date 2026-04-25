/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fat16_fs.c
 * Description: FAT16-like backend registration built over the simplefs engine.
 */

#include "fat16_fs.h"
#include "simplefs.h"

int fat16_fs_register(void)
{
    return simplefs_engine_register("fat16", 1);
}
