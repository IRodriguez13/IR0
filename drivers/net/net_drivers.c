/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: net_drivers.c
 * Description: The NIC driver table and its probe loop.
 *
 * Adding a NIC: implement struct net_device_ops in drivers/net/<card>.c,
 * export one const struct net_driver from it (declared in net_drivers.h),
 * and list it here under its Kconfig symbol. Nothing outside drivers/net
 * changes.
 */

#include "net_drivers.h"

#include <ir0/driver_backend.h>
#include <ir0/logging.h>
#include <ir0/net.h>
#include <stddef.h>

/*
 * Probe order: PCI cards on the legacy PC platform, firmware/device-tree
 * described cards elsewhere.
 */
static const struct net_driver *const net_drivers[] = {
#if IR0_LEGACY_PC_IO
#if CONFIG_DRV_NIC_VIRTIO_NET
	&virtio_net_driver,
#endif
#if CONFIG_DRV_NIC_RTL8139
	&rtl8139_driver,
#endif
#else
#if CONFIG_DRV_NIC_BCMGENET
	&bcmgenet_driver,
#endif
#endif
	NULL,
};

/*
 * Probe every configured driver. An absent or failed card never stops the
 * stack: each driver logs its own outcome.
 */
int net_stack_probe_drivers(void)
{
	const struct net_driver *const *drv;

	for (drv = net_drivers; *drv; drv++)
	{
		int rc = (*drv)->probe();

		LOG_DEBUG_FMT("NET", "probe %s: %d", (*drv)->name, rc);
	}
	return 0;
}
