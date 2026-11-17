/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: net_drivers_dt.c
 * Description: The device-tree platforms' NICs, probed in this order.
 */

#include "net_drivers.h"

#include <config.h>
#include <stddef.h>

const struct net_driver *const net_drivers[] = {
#if CONFIG_DRV_NIC_BCMGENET
	&bcmgenet_driver,
#endif
	NULL,
};
