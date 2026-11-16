/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: netdevice.h
 * Description: Network device class API, after Linux <linux/netdevice.h>.
 *
 * Every NIC driver fills one struct net_device and one const
 * struct net_device_ops and registers it with register_netdev(). Protocols,
 * socket ioctls and the pseudo filesystems use only this header: they never
 * call a driver. An operation a card does not have is a NULL op, and the core
 * answers for it (dev_set_mac_address() returns -EOPNOTSUPP), so callers never test which driver is behind a device.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define ETH_ALEN	6
#define ETH_MIN_MTU	68
#define ETH_DATA_LEN	1500
#define IP_MAX_MTU	65535

/*
 * Linux uapi netdevice flags (include/uapi/linux/if.h).
 * BusyBox ifconfig/route interpret these bits; IR0 must match.
 */
#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_NOTRAILERS  0x20
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_ALLMULTI    0x200
#define IFF_MULTICAST   0x1000

struct net_device;

/*
 * Interface counters (Linux struct net_device_stats subset). The driver
 * counts packets, bytes, errors and FIFO overruns; netif_rx() counts
 * multicast frames and the frames no protocol takes (rx_dropped).
 */
struct net_device_stats
{
	uint64_t rx_packets;
	uint64_t tx_packets;
	uint64_t rx_bytes;
	uint64_t tx_bytes;
	uint64_t rx_errors;
	uint64_t tx_errors;
	uint64_t rx_dropped;
	uint64_t rx_fifo_errors;
	uint64_t multicast;
};

/*
 * Driver operations. ndo_start_xmit is mandatory; every other op is optional
 * and NULL means the card does not have it.
 *
 * @ndo_start_xmit:      send one complete Ethernet frame (header included).
 *                       The frame belongs to the caller once this returns.
 *                       Returns 0 or a negative errno.
 * @ndo_set_mac_address: change the station address. NULL: -EOPNOTSUPP.
 *                       eth_mac_addr() suits cards that need no register
 *                       write (they receive in promiscuous mode).
 * @ndo_poll_controller: receive and reap transmit completions without an
 *                       interrupt. The core calls it from net_stack_poll()
 *                       and, for devices without an IRQ line, from every
 *                       interrupt the stack sees.
 */
struct net_device_ops
{
	int (*ndo_start_xmit)(struct net_device *dev, const void *frame,
			      size_t len);
	int (*ndo_set_mac_address)(struct net_device *dev, const uint8_t *addr);
	void (*ndo_poll_controller)(struct net_device *dev);
};

struct net_device
{
	const char *name;
	uint8_t dev_addr[ETH_ALEN];
	unsigned int flags;		/* IFF_* */
	unsigned int mtu;
	int irq;			/* line from irq_request(), or -1 */
	struct net_device_stats stats;
	const struct net_device_ops *netdev_ops;
	struct net_device *next;
};

/* Allocation and registration (one path for every driver). */
struct net_device *alloc_etherdev(size_t sizeof_priv);
void free_netdev(struct net_device *dev);
void *netdev_priv(const struct net_device *dev);
int register_netdev(struct net_device *dev);
void unregister_netdev(struct net_device *dev);

/* Receive path: a driver hands every received Ethernet frame to the stack. */
void netif_rx(struct net_device *dev, const void *frame, size_t len);

/* Administrative control (SIOCSIF*, /dev/net, in-kernel autoconfiguration). */
int dev_change_flags(struct net_device *dev, unsigned int flags);
int dev_set_mtu(struct net_device *dev, int new_mtu);
int dev_set_mac_address(struct net_device *dev, const uint8_t *addr);
int eth_mac_addr(struct net_device *dev, const uint8_t *addr);

/* Lookup. Devices are listed newest first. */
struct net_device *first_net_device(void);
struct net_device *dev_get_by_name(const char *name);

static inline struct net_device *next_net_device(const struct net_device *dev)
{
	return dev->next;
}

#define for_each_netdev(dev) \
	for ((dev) = first_net_device(); (dev); (dev) = next_net_device(dev))
