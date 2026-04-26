/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_early.c
 * Description: ARM64 early init hooks.
 */

void arch_early_init_arm64(void)
{
    /* Stub: board/SoC-specific EL and MMU setup is implemented during bring-up. */
}
