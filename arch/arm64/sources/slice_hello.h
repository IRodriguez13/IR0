/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: slice_hello.h
 * Description: F7b.1 post-MMU portable slice hook (ARM64 freestanding boot).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/** Called after early MMU enable; prints ARM64_SLICE_OK on PL011. */
void arm64_slice_after_mmu(void);
