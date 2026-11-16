/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: dev.c
 * Description: Network device class core, after Linux net/core/dev.c.
 *
 * One registration path for every NIC (alloc_etherdev + register_netdev),
 * the device list and its lookups, and the operations a caller asks of a
 * device through the core: transmit, poll, interrupt, MTU and address
 * changes. A NULL optional op gets the Linux answer here, so protocols and
 * the pseudo filesystems never test which driver is behind a device.
 */

#include <ir0/errno.h>
#include <ir0/irq.h>
#include <ir0/klog.h>
#include <ir0/kmem.h>
#include <ir0/net.h>
#include <ir0/netdevice.h>
#include <string.h>

/* Private area alignment after struct net_device (Linux NETDEV_ALIGN). */
/* dev->state bits (Linux enum netdev_state_t). */
#define LINK_STATE_START	(1UL << 0)	/* dev_open() done */
#define LINK_STATE_NOCARRIER	(1UL << 1)	/* driver: no link */

#define NETDEV_ALIGN	32U
#define NETDEV_PRIV_OFFSET \
	((sizeof(struct net_device) + NETDEV_ALIGN - 1U) & ~(size_t)(NETDEV_ALIGN - 1U))

/*
 * Registered devices, newest first. Drivers register while probing, before
 * interrupts are enabled, so the list has no lock.
 */
static struct net_device *devices;

/**
 * alloc_etherdev - allocate an Ethernet device with @sizeof_priv driver bytes
 *
 * The device starts as Linux ether_setup() leaves it: 1500-byte MTU,
 * IFF_BROADCAST, no IRQ line. netdev_priv() returns the driver area.
 */
struct net_device *alloc_etherdev(size_t sizeof_priv)
{
	struct net_device *dev;

	dev = kmalloc(NETDEV_PRIV_OFFSET + sizeof_priv);
	if (!dev)
		return NULL;
	memset(dev, 0, NETDEV_PRIV_OFFSET + sizeof_priv);
	dev->mtu = ETH_DATA_LEN;
	dev->flags = IFF_BROADCAST;
	dev->irq = -1;
	return dev;
}

void free_netdev(struct net_device *dev)
{
	if (dev)
		kfree(dev);
}

void *netdev_priv(const struct net_device *dev)
{
	return (char *)dev + NETDEV_PRIV_OFFSET;
}

/**
 * register_netdev - publish @dev to the stack
 *
 * The device needs a name and an ops table with ndo_start_xmit. Registering
 * the same device twice is a no-op.
 *
 * Return: 0, or -EINVAL for an incomplete device.
 */
int register_netdev(struct net_device *dev)
{
	struct net_device *curr;

	if (!dev || !dev->name || !dev->netdev_ops ||
	    !dev->netdev_ops->ndo_start_xmit)
		return -EINVAL;

	for (curr = devices; curr; curr = curr->next)
	{
		if (curr == dev)
		{
			klog_info_fmt("NET", "Device already registered: %s",
				      dev->name);
			return 0;
		}
	}

	dev->next = devices;
	devices = dev;
	klog_info_fmt("NET", "Registered device: %s", dev->name);
	return 0;
}

void unregister_netdev(struct net_device *dev)
{
	struct net_device **link;

	if (!dev)
		return;
	for (link = &devices; *link; link = &(*link)->next)
	{
		if (*link == dev)
		{
			*link = dev->next;
			dev->next = NULL;
			return;
		}
	}
}

struct net_device *first_net_device(void)
{
	return devices;
}

struct net_device *dev_get_by_name(const char *name)
{
	struct net_device *dev;

	if (!name)
		return NULL;
	for_each_netdev(dev)
	{
		if (strcmp(dev->name, name) == 0)
			return dev;
	}
	return NULL;
}

/* IFF_RUNNING follows "up with a carrier" (Linux dev_get_flags()). */
static void dev_update_running(struct net_device *dev)
{
	if (netif_running(dev) && netif_carrier_ok(dev))
		dev->flags |= IFF_RUNNING;
	else
		dev->flags &= ~IFF_RUNNING;
}

void netif_carrier_on(struct net_device *dev)
{
	dev->state &= ~LINK_STATE_NOCARRIER;
	dev_update_running(dev);
}

void netif_carrier_off(struct net_device *dev)
{
	dev->state |= LINK_STATE_NOCARRIER;
	dev_update_running(dev);
}

bool netif_carrier_ok(const struct net_device *dev)
{
	return (dev->state & LINK_STATE_NOCARRIER) == 0;
}

bool netif_running(const struct net_device *dev)
{
	return (dev->state & LINK_STATE_START) != 0;
}

/**
 * dev_open - bring @dev up: start the hardware (ndo_open) and set IFF_UP
 *
 * Return: 0, or the driver's ndo_open error (the device stays down).
 */
int dev_open(struct net_device *dev)
{
	int rc;

	if (!dev)
		return -EINVAL;
	if (netif_running(dev))
		return 0;
	if (dev->netdev_ops && dev->netdev_ops->ndo_open)
	{
		rc = dev->netdev_ops->ndo_open(dev);
		if (rc < 0)
			return rc;
	}
	dev->state |= LINK_STATE_START;
	dev->flags |= IFF_UP;
	dev_update_running(dev);
	return 0;
}

/* dev_close - quiesce the hardware (ndo_stop) and clear IFF_UP. */
int dev_close(struct net_device *dev)
{
	if (!dev)
		return -EINVAL;
	if (!netif_running(dev))
		return 0;
	if (dev->netdev_ops && dev->netdev_ops->ndo_stop)
		(void)dev->netdev_ops->ndo_stop(dev);
	dev->state &= ~LINK_STATE_START;
	dev->flags &= ~IFF_UP;
	dev_update_running(dev);
	return 0;
}

/**
 * dev_change_flags - apply interface flags from SIOCSIFFLAGS or /dev/net
 *
 * IFF_UP opens or closes the device; IFF_RUNNING is the core's and is
 * ignored from the caller; every other bit is stored as given.
 */
int dev_change_flags(struct net_device *dev, unsigned int flags)
{
	const unsigned int core = IFF_UP | IFF_RUNNING;

	if (!dev)
		return -EINVAL;
	dev->flags = (flags & ~core) | (dev->flags & core);
	if (flags & IFF_UP)
		return dev_open(dev);
	return dev_close(dev);
}

int dev_set_mtu(struct net_device *dev, int new_mtu)
{
	if (!dev)
		return -EINVAL;
	if (new_mtu < ETH_MIN_MTU || new_mtu > IP_MAX_MTU)
		return -EINVAL;
	dev->mtu = (unsigned int)new_mtu;
	return 0;
}

/**
 * dev_set_mac_address - change the station address through the driver
 *
 * Return: the driver's result, or -EOPNOTSUPP when the card has no
 * ndo_set_mac_address.
 */
int dev_set_mac_address(struct net_device *dev, const uint8_t *addr)
{
	if (!dev || !addr)
		return -EINVAL;
	if (!dev->netdev_ops || !dev->netdev_ops->ndo_set_mac_address)
		return -EOPNOTSUPP;
	return dev->netdev_ops->ndo_set_mac_address(dev, addr);
}

/* ndo_set_mac_address for cards that filter nothing by address. */
int eth_mac_addr(struct net_device *dev, const uint8_t *addr)
{
	memcpy(dev->dev_addr, addr, ETH_ALEN);
	return 0;
}

static void dev_poll_controller(struct net_device *dev)
{
	if (dev->netdev_ops->ndo_poll_controller)
		dev->netdev_ops->ndo_poll_controller(dev);
}

void net_stack_poll(void)
{
	struct net_device *dev;

	for_each_netdev(dev)
		dev_poll_controller(dev);
}

int net_stack_get_irq_line(void)
{
	struct net_device *dev;

	for_each_netdev(dev)
	{
		if (dev->irq >= 0)
			return dev->irq;
	}
	return -1;
}

/*
 * A line a NIC requested runs its irq_request() handler once; devices
 * without a line are polled, so they still receive between idle polls.
 */
int net_stack_handle_irq(uint8_t irq)
{
	struct net_device *dev;
	int handled = 0;

	for_each_netdev(dev)
	{
		if (dev->irq < 0)
			dev_poll_controller(dev);
		else if (dev->irq == (int)irq && !handled)
			handled = irq_dispatch(irq) == 0;
	}
	return handled;
}
