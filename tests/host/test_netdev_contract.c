/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_netdev_contract.c
 * Description: netdev_contract_suite — the network device class core
 *              (net/dev.c) against fake drivers: registration, lookup,
 *              optional ops, MTU range, IRQ dispatch, polling and link
 *              state.
 */

#include "test_harness_ir0.h"
#include <ir0/errno.h>
#include <ir0/irq.h>
#include <ir0/net.h>
#include <ir0/netdevice.h>
#include <string.h>

static int g_poll_calls;
static int g_irq_calls;
static int g_open_rc;
static int g_stop_calls;

static int fake_xmit(struct net_device *dev, const void *frame, size_t len)
{
	(void)dev;
	(void)frame;
	(void)len;
	return 0;
}

static void fake_poll(struct net_device *dev)
{
	(void)dev;
	g_poll_calls++;
}

static void fake_irq(uint32_t irq, void *data)
{
	(void)irq;
	(void)data;
	g_irq_calls++;
}

static int fake_open(struct net_device *dev)
{
	(void)dev;
	return g_open_rc;
}

static int fake_stop(struct net_device *dev)
{
	(void)dev;
	g_stop_calls++;
	return 0;
}

/* A card with every optional op missing. */
static const struct net_device_ops minimal_ops = {
	.ndo_start_xmit = fake_xmit,
};

/* A card that polls and accepts a software address change. */
static const struct net_device_ops polled_ops = {
	.ndo_start_xmit = fake_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_poll_controller = fake_poll,
};

/* A card that starts and stops its hardware with the interface. */
static const struct net_device_ops stateful_ops = {
	.ndo_open = fake_open,
	.ndo_stop = fake_stop,
	.ndo_start_xmit = fake_xmit,
};

void test_netdev_contract(void)
{
	static const uint8_t addr[ETH_ALEN] = { 0x02, 0, 0, 0, 0, 0x42 };
	struct net_device *a;
	struct net_device *b;
	struct net_device *dev;
	int n;

	TEST_BEGIN("netdev_alloc_ether_defaults");
	a = alloc_etherdev(64);
	ASSERT(a != NULL);
	ASSERT_EQ(a->mtu, (unsigned int)ETH_DATA_LEN);
	ASSERT_EQ(a->flags, (unsigned int)IFF_BROADCAST);
	ASSERT_EQ(a->irq, -1);
	ASSERT(((uintptr_t)netdev_priv(a) & 31U) == 0);
	ASSERT((char *)netdev_priv(a) >= (char *)a + sizeof(*a));
	TEST_END();

	TEST_BEGIN("netdev_register_requires_name_and_xmit");
	ASSERT_EQ(register_netdev(a), -EINVAL);
	a->name = "fake0";
	ASSERT_EQ(register_netdev(a), -EINVAL);
	a->netdev_ops = &minimal_ops;
	ASSERT_EQ(register_netdev(a), 0);
	ASSERT_EQ(register_netdev(a), 0);
	ASSERT(first_net_device() == a);
	ASSERT(next_net_device(a) == NULL);
	TEST_END();

	TEST_BEGIN("netdev_lookup_newest_first");
	b = alloc_etherdev(0);
	ASSERT(b != NULL);
	b->name = "fake1";
	b->netdev_ops = &polled_ops;
	ASSERT_EQ(register_netdev(b), 0);
	ASSERT(first_net_device() == b);
	ASSERT(next_net_device(b) == a);
	ASSERT(dev_get_by_name("fake0") == a);
	ASSERT(dev_get_by_name("fake1") == b);
	ASSERT(dev_get_by_name("fake2") == NULL);
	ASSERT(dev_get_by_name(NULL) == NULL);
	n = 0;
	for_each_netdev(dev)
		n++;
	ASSERT_EQ(n, 2);
	TEST_END();

	TEST_BEGIN("netdev_optional_mac_op");
	ASSERT_EQ(dev_set_mac_address(a, addr), -EOPNOTSUPP);
	ASSERT_EQ(a->dev_addr[5], 0);
	ASSERT_EQ(dev_set_mac_address(b, addr), 0);
	ASSERT(memcmp(b->dev_addr, addr, ETH_ALEN) == 0);
	TEST_END();

	TEST_BEGIN("netdev_mtu_range");
	ASSERT_EQ(dev_set_mtu(a, ETH_MIN_MTU - 1), -EINVAL);
	ASSERT_EQ(dev_set_mtu(a, IP_MAX_MTU + 1), -EINVAL);
	ASSERT_EQ(a->mtu, (unsigned int)ETH_DATA_LEN);
	ASSERT_EQ(dev_set_mtu(a, 576), 0);
	ASSERT_EQ(a->mtu, 576U);
	TEST_END();

	TEST_BEGIN("netdev_poll_skips_cards_without_op");
	g_poll_calls = 0;
	net_stack_poll();
	ASSERT_EQ(g_poll_calls, 1);
	TEST_END();

	TEST_BEGIN("netdev_irq_dispatch_and_poll_fallback");
	ASSERT_EQ(irq_request(210, fake_irq, a, "fake0"), 0);
	a->irq = 210;
	ASSERT_EQ(net_stack_get_irq_line(), 210);
	g_irq_calls = 0;
	g_poll_calls = 0;
	ASSERT_EQ(net_stack_handle_irq(210), 1);
	ASSERT_EQ(g_irq_calls, 1);
	ASSERT_EQ(g_poll_calls, 1);
	ASSERT_EQ(net_stack_handle_irq(211), 0);
	ASSERT_EQ(g_irq_calls, 1);
	ASSERT_EQ(g_poll_calls, 2);
	TEST_END();

	TEST_BEGIN("netdev_unregister");
	unregister_netdev(b);
	ASSERT(first_net_device() == a);
	ASSERT(dev_get_by_name("fake1") == NULL);
	unregister_netdev(a);
	ASSERT(first_net_device() == NULL);
	ASSERT_EQ(net_stack_get_irq_line(), -1);
	free_netdev(a);
	free_netdev(b);
	TEST_END();

	TEST_BEGIN("netdev_down_is_not_running");
	/* BusyBox "ifconfig eth0 down": SIOCSIFFLAGS with the read flags
	 * minus IFF_UP, so IFF_RUNNING comes back from userspace. */
	a = alloc_etherdev(0);
	ASSERT(a != NULL);
	a->name = "fake2";
	a->netdev_ops = &minimal_ops;
	ASSERT_EQ(dev_change_flags(a, IFF_UP | IFF_BROADCAST), 0);
	ASSERT(a->flags & IFF_UP);
	ASSERT(a->flags & IFF_RUNNING);
	ASSERT_EQ(dev_change_flags(a, IFF_BROADCAST | IFF_RUNNING), 0);
	ASSERT(!(a->flags & IFF_UP));
	ASSERT(!(a->flags & IFF_RUNNING));
	ASSERT(a->flags & IFF_BROADCAST);
	free_netdev(a);
	TEST_END();

	TEST_BEGIN("netdev_carrier_and_open");
	a = alloc_etherdev(0);
	ASSERT(a != NULL);
	a->name = "fake3";
	a->netdev_ops = &stateful_ops;
	ASSERT(netif_carrier_ok(a));
	ASSERT(!netif_running(a));
	netif_carrier_off(a);
	g_open_rc = -EIO;
	ASSERT_EQ(dev_open(a), -EIO);
	ASSERT(!netif_running(a));
	ASSERT(!(a->flags & IFF_UP));
	g_open_rc = 0;
	ASSERT_EQ(dev_open(a), 0);
	ASSERT(netif_running(a));
	ASSERT(a->flags & IFF_UP);
	ASSERT(!(a->flags & IFF_RUNNING));
	netif_carrier_on(a);
	ASSERT(a->flags & IFF_RUNNING);
	netif_carrier_off(a);
	ASSERT(!(a->flags & IFF_RUNNING));
	netif_carrier_on(a);
	g_stop_calls = 0;
	ASSERT_EQ(dev_close(a), 0);
	ASSERT_EQ(g_stop_calls, 1);
	ASSERT(!(a->flags & (IFF_UP | IFF_RUNNING)));
	ASSERT_EQ(dev_close(a), 0);
	ASSERT_EQ(g_stop_calls, 1);
	free_netdev(a);
	TEST_END();

	TEST_BEGIN("network_byte_order_contract");
	ASSERT_EQ(ntohs(htons(0x1234U)), 0x1234U);
	ASSERT_EQ(ntohl(htonl(0x12345678U)), 0x12345678U);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	ASSERT_EQ(htons(0x1234U), 0x3412U);
	ASSERT_EQ(htonl(0x12345678U), 0x78563412U);
#else
	ASSERT_EQ(htons(0x1234U), 0x1234U);
	ASSERT_EQ(htonl(0x12345678U), 0x12345678U);
#endif
	TEST_END();
}
