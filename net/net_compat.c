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
 * Fallbacks linked instead of net/dev.c when networking is disabled.
 */

#include <ir0/errno.h>
#include <ir0/net.h>
#include <ir0/netdevice.h>

void net_stack_poll(void)
{
}

int net_stack_get_irq_line(void)
{
    return -1;
}

int net_stack_handle_irq(uint8_t irq)
{
    (void)irq;
    return 0;
}

struct net_device *first_net_device(void)
{
    return (struct net_device *)0;
}

/* Socket ioctls look interfaces up by name; without a stack there are none. */
struct net_device *dev_get_by_name(const char *name)
{
    (void)name;
    return (struct net_device *)0;
}

int dev_change_flags(struct net_device *dev, unsigned int flags)
{
    (void)dev;
    (void)flags;
    return -ENODEV;
}

int dev_set_mtu(struct net_device *dev, int new_mtu)
{
    (void)dev;
    (void)new_mtu;
    return -ENODEV;
}

int dev_set_mac_address(struct net_device *dev, const uint8_t *addr)
{
    (void)dev;
    (void)addr;
    return -ENODEV;
}
