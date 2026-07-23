/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: net_drivers.c
 * Description: Network driver probe registry.
 */

#include <config.h>
#if CONFIG_DRV_NIC_RTL8139
#include <drivers/net/rtl8139.h>
#endif
#if CONFIG_DRV_NIC_VIRTIO_NET
#include <drivers/net/virtio_net.h>
#endif

int net_stack_probe_drivers(void)
{
    int ret = 0;

#if CONFIG_DRV_NIC_VIRTIO_NET
    if (virtio_net_init() != 0)
    {
        /* Absent is OK when QEMU only presents rtl8139. */
    }
#endif
#if CONFIG_DRV_NIC_RTL8139
    if (rtl8139_init() != 0)
        ret = -1;
#endif

    return ret;
}
