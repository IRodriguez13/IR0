/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dhcp.h
 * Description: DHCPv4 client API for runtime network configuration
 */

#pragma once

#include <ir0/net.h>

/* DHCP client entry point (best effort; keeps static config on failure). */
int dhcp_init(void);

