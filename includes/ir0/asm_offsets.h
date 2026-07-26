/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: asm_offsets.h
 * Description: Single source of truth for C↔ASM structure offsets (x86-64).
 *
 * Keep in sync with includes/ir0/asm_offsets.inc (NASM) — arch-guard verifies.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/* process_t.fs_base — switch_x64.asm PROC_FS_BASE_OFFSET */
#define IR0_PROC_FS_BASE_OFFSET 0x140

/* task_t.arch field offsets (arch_task_context_x86_64 layout) */
#define IR0_TASK_ARCH_RIP_OFFSET 0x80
#define IR0_TASK_ARCH_CR3_OFFSET 0xB0
#define IR0_TASK_ARCH_SS_OFFSET  0x9A
