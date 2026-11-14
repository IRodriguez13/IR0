/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net_compat.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Networking compatibility stubs
 *
 * Weak fallbacks used when networking backend is not linked.
 * Real implementations in net/net.c override these symbols.
 */

#include <ir0/errno.h>
#include <ir0/net.h>

__attribute__((weak)) void net_stack_poll(void)
{
}

__attribute__((weak)) int net_stack_get_irq_line(void)
{
    return -1;
}

__attribute__((weak)) int net_stack_handle_irq(uint8_t irq)
{
    (void)irq;
    return 0;
}

__attribute__((weak)) struct net_device *net_get_devices(void)
{
    return (struct net_device *)0;
}
