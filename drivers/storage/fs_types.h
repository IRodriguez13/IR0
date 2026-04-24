/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fs_types.h
 * Description: IR0 kernel source/header file
 */

#ifndef _FS_TYPES_H
#define _FS_TYPES_H

#include <stdint.h>

// Filesystem type identification
const char* get_fs_type(uint8_t system_id);

#endif /* _FS_TYPES_H */