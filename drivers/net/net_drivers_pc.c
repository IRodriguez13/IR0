/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: net_drivers_pc.c
 * Description: The PC platform's NICs: PCI cards, probed in this order.
 */

#include "net_drivers.h"

#include <config.h>
#include <stddef.h>

const struct net_driver *const net_drivers[] = {
#if CONFIG_DRV_NIC_VIRTIO_NET
	&virtio_net_driver,
#endif
#if CONFIG_DRV_NIC_RTL8139
	&rtl8139_driver,
#endif
	NULL,
};
