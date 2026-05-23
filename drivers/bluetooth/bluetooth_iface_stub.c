/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: bluetooth_iface_stub.c
 * Description: Temporary link-time stubs for Bluetooth facade when subsystem
 * is excluded from build through Makefile config overrides.
 */

#include <ir0/bluetooth.h>
#include <ir0/errno.h>

int ir0_bluetooth_register_driver(void)
{
	return -ENOSYS;
}

int ir0_bt_proc_devices_read(char *buffer, size_t count)
{
	(void)buffer;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_proc_scan_read(char *buffer, size_t count)
{
	(void)buffer;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_proc_scan_write(const char *command)
{
	(void)command;
	return -ENOSYS;
}

int ir0_bt_sysfs_hci0_address_read(char *buf, size_t count)
{
	(void)buf;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_sysfs_hci0_state_read(char *buf, size_t count)
{
	(void)buf;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_sysfs_topology_neighbors_read(char *buf, size_t count)
{
	(void)buf;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_sysfs_sessions_read(char *buf, size_t count)
{
	(void)buf;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_hci_open(void)
{
	return -ENOSYS;
}

int ir0_bt_hci_close(void)
{
	return -ENOSYS;
}

int ir0_bt_hci_read(char *buffer, size_t count)
{
	(void)buffer;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_hci_write(const char *buffer, size_t count)
{
	(void)buffer;
	(void)count;
	return -ENOSYS;
}

int ir0_bt_hci_ioctl(unsigned int cmd, unsigned long arg)
{
	(void)cmd;
	(void)arg;
	return -ENOSYS;
}

void ir0_bluetooth_poll(void)
{
}
