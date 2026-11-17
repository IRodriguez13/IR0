/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: net_drivers.h
 * Description: NIC driver table entries. Private to drivers/net: each
 *              driver exports one const struct net_driver and nothing else;
 *              the rest of the kernel reaches its devices only through
 *              <ir0/netdevice.h>.
 */

#pragma once

/*
 * @name:  driver name for the probe trace.
 * @probe: find the hardware (PCI scan, or the firmware/device-tree MMIO
 *         description), then alloc_etherdev() + register_netdev().
 *         Returns 0, -ENODEV when the hardware is absent, or another
 *         negative errno after logging the failure.
 */
struct net_driver
{
	const char *name;
	int (*probe)(void);
};

/*
 * The platform's NICs in probe order, NULL-terminated. Each platform lists
 * its cards in drivers/net/net_drivers_<platform>.c, picked by the build.
 */
extern const struct net_driver *const net_drivers[];

extern const struct net_driver bcmgenet_driver;
extern const struct net_driver rtl8139_driver;
extern const struct net_driver virtio_net_driver;
