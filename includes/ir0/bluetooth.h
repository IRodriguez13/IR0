/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bluetooth.h
 * Description: Bluetooth facade for fs/dev interfaces.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

int ir0_bt_proc_devices_read(char *buffer, size_t count);
int ir0_bt_proc_scan_read(char *buffer, size_t count);
int ir0_bt_proc_scan_write(const char *command);

int ir0_bt_sysfs_hci0_address_read(char *buf, size_t count);
int ir0_bt_sysfs_hci0_state_read(char *buf, size_t count);
int ir0_bt_sysfs_topology_neighbors_read(char *buf, size_t count);
int ir0_bt_sysfs_sessions_read(char *buf, size_t count);

int ir0_bt_hci_open(void);
int ir0_bt_hci_close(void);
int ir0_bt_hci_read(char *buffer, size_t count);
int ir0_bt_hci_write(const char *buffer, size_t count);
int ir0_bt_hci_ioctl(unsigned int cmd, unsigned long arg);
