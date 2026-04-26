/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: net_drivers.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Network driver probe registry
 *
 * Keeps net stack core decoupled from concrete NIC implementations.
 */

#include <config.h>
#if CONFIG_DRV_NIC_RTL8139
#include <drivers/net/rtl8139.h>
#endif

int net_stack_probe_drivers(void)
{
    int ret = 0;

#if CONFIG_DRV_NIC_RTL8139
    if (rtl8139_init() != 0)
        ret = -1;
#endif

    return ret;
}

