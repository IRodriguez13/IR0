/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bt_sysfs.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Bluetooth sysfs interface
 * Copyright (C) 2025  Iván Rodriguez
 */

#pragma once

#include <stddef.h>

int bt_sysfs_hci0_address_read(char *buf, size_t count);
int bt_sysfs_hci0_state_read(char *buf, size_t count);
int bt_sysfs_topology_neighbors_read(char *buf, size_t count);
int bt_sysfs_sessions_read(char *buf, size_t count);
