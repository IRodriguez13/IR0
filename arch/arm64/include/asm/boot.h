/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot.h
 * Description: ARM64 boot constants shared by entry.S and C.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Stack used by kmain() until the first task switch. The common boot path
 * (KTM suite, MM and VFS init) needs more than 4 KiB; 16 KiB matches x86-64
 * (boot_x64.asm). Must stay a multiple of 4 KiB for the entry.S add.
 */
#define ARM64_BOOT_STACK_SIZE 0x4000
