/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bluetooth_iface.c
 * Description: Bluetooth facade implementation for fs/dev callsites.
 */

#include <ir0/bluetooth.h>
#include "bt_device.h"
#include "bt_sysfs.h"

int ir0_bt_proc_devices_read(char *buffer, size_t count)
{
	return bt_proc_devices_read(buffer, count);
}

int ir0_bt_proc_scan_read(char *buffer, size_t count)
{
	return bt_proc_scan_read(buffer, count);
}

int ir0_bt_proc_scan_write(const char *command)
{
	return bt_proc_scan_write(command);
}

int ir0_bt_sysfs_hci0_address_read(char *buf, size_t count)
{
	return bt_sysfs_hci0_address_read(buf, count);
}

int ir0_bt_sysfs_hci0_state_read(char *buf, size_t count)
{
	return bt_sysfs_hci0_state_read(buf, count);
}

int ir0_bt_sysfs_topology_neighbors_read(char *buf, size_t count)
{
	return bt_sysfs_topology_neighbors_read(buf, count);
}

int ir0_bt_sysfs_sessions_read(char *buf, size_t count)
{
	return bt_sysfs_sessions_read(buf, count);
}

int ir0_bt_hci_open(void)
{
	return bt_hci_open();
}

int ir0_bt_hci_close(void)
{
	return bt_hci_close();
}

int ir0_bt_hci_read(char *buffer, size_t count)
{
	return bt_hci_read(buffer, count);
}

int ir0_bt_hci_write(const char *buffer, size_t count)
{
	return bt_hci_write(buffer, count);
}

int ir0_bt_hci_ioctl(unsigned int cmd, unsigned long arg)
{
	return bt_hci_ioctl(cmd, arg);
}
