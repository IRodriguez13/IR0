/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_netdev_contract.c
 * Description: netdev_contract_suite — mandatory net_device ops (§7/§9).
 */

#include "test_harness_ir0.h"
#include <ir0/net.h>
#include <string.h>

static int mock_send(struct net_device *dev, void *data, size_t len)
{
	(void)dev;
	(void)data;
	(void)len;
	return (int)len;
}

static void mock_poll(struct net_device *dev)
{
	(void)dev;
}

void test_netdev_contract(void)
{
	struct net_device dev;

	TEST_BEGIN("netdev_contract_mandatory_ops");
	memset(&dev, 0, sizeof(dev));
	dev.send = mock_send;
	dev.poll = mock_poll;
	ASSERT(dev.send != NULL);
	ASSERT(dev.poll != NULL);
	ASSERT(dev.send(&dev, "x", 1) == 1);
	dev.poll(&dev);
	TEST_END();

	TEST_BEGIN("netdev_contract_optional_stats_null_ok");
	ASSERT(dev.get_stats == NULL);
	ASSERT(dev.get_byte_stats == NULL);
	ASSERT(dev.handle_irq == NULL);
	TEST_END();
}
