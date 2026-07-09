/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: clone.h
 * Description: Linux clone(2) flags (musl pthread ABI)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#define CLONE_VM              0x00000100UL
#define CLONE_FS              0x00000200UL
#define CLONE_FILES           0x00000400UL
#define CLONE_SIGHAND         0x00000800UL
#define CLONE_THREAD          0x00010000UL
#define CLONE_SETTLS          0x00080000UL
#define CLONE_PARENT_SETTID   0x00100000UL
#define CLONE_CHILD_CLEARTID  0x00200000UL
#define CLONE_CHILD_SETTID    0x01000000UL
