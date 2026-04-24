/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_x64.c
 * Description: IR0 kernel source/header file
 */

/* arch/x86-64/sources/arch_x64.c - Architecture setup functions */

/*
 * arch_x64_init is not referenced anywhere in the tree; GDT/TSS install
 * runs from the active boot path without calling this symbol.
 */
void arch_x64_init(void)
{
}
