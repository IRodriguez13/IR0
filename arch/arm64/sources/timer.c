/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: timer.c
 * Description: ARM64 timer scaffold.
 */

#include <stdint.h>

void arch_timer_init(void)
{
    /* Stub: initialize architectural timer for target board. */
}

uint64_t arch_timer_read(void)
{
    return 0;
}

void arch_timer_set_frequency(uint32_t hz)
{
    (void)hz;
}

uint32_t arch_timer_get_frequency(void)
{
    return 0;
}
