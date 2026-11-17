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
 * and list it in the platform's table (net_drivers_<platform>.c) under its
 * Kconfig symbol. Nothing outside drivers/net changes.
 */

#include "net_drivers.h"

#include <ir0/logging.h>
#include <ir0/net.h>
#include <stddef.h>

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
