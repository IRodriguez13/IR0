/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Network driver probe registry
 *
 * Keeps net stack core decoupled from concrete NIC implementations.
 */

#include <drivers/net/rtl8139.h>

int net_stack_probe_drivers(void)
{
    int ret = 0;

    if (rtl8139_init() != 0)
        ret = -1;

    return ret;
}

