/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: net_drivers.c
 * Description: Network driver probe registry.
 */

#include <config.h>
#include <ir0/driver_backend.h>
#if CONFIG_DRV_NIC_RTL8139
#include <drivers/net/rtl8139.h>
#endif
#if CONFIG_DRV_NIC_VIRTIO_NET && IR0_LEGACY_PC_IO
#include <drivers/net/virtio_net.h>
#endif
#if CONFIG_DRV_NIC_BCMGENET
#include <drivers/net/bcmgenet.h>
#endif

#if !IR0_LEGACY_PC_IO
extern int arm64_virtio_net_init(void);
#endif

int net_stack_probe_drivers(void)
{
	int ret = 0;

#if !IR0_LEGACY_PC_IO
#if CONFIG_DRV_NIC_BCMGENET
	(void)bcmgenet_init();
#endif
	(void)arm64_virtio_net_init();
	return ret;
#else

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
#endif
}
