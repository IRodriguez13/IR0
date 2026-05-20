/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_bt_scan_sync.c
 * Description: Host-side stub mirroring bt_scan_state_sync (bt_device.c) for inquiry flag alignment.
 */

#include "test_harness.h"

/*
 * Mirrors the essential rule in drivers/bluetooth/bt_device.c:
 * if (bt_manager->scan_active && !hdev->scanning) bt_manager->scan_active = 0;
 * without linking the full Bluetooth stack on the host.
 */
struct host_bt_scan_stub
{
	int manager_ok;
	int hci_present;
	int scan_active;
	int hci_scanning;
};

static struct host_bt_scan_stub g_bt_stub;

static void host_bt_scan_stub_reset(void)
{
	g_bt_stub.manager_ok = 1;
	g_bt_stub.hci_present = 1;
	g_bt_stub.scan_active = 0;
	g_bt_stub.hci_scanning = 0;
}

static void host_bt_scan_state_sync(void)
{
	if (!g_bt_stub.manager_ok)
		return;
	if (!g_bt_stub.hci_present)
		return;
	if (g_bt_stub.scan_active && !g_bt_stub.hci_scanning)
		g_bt_stub.scan_active = 0;
}

void test_bt_scan_sync_stub(void)
{
	TEST_BEGIN("bt_scan_sync_stub");

	host_bt_scan_stub_reset();
	g_bt_stub.scan_active = 1;
	g_bt_stub.hci_scanning = 0;
	host_bt_scan_state_sync();
	ASSERT_EQ(g_bt_stub.scan_active, 0);

	host_bt_scan_stub_reset();
	g_bt_stub.scan_active = 1;
	g_bt_stub.hci_scanning = 1;
	host_bt_scan_state_sync();
	ASSERT_EQ(g_bt_stub.scan_active, 1);

	host_bt_scan_stub_reset();
	g_bt_stub.manager_ok = 0;
	g_bt_stub.scan_active = 1;
	g_bt_stub.hci_scanning = 0;
	host_bt_scan_state_sync();
	ASSERT_EQ(g_bt_stub.scan_active, 1);

	host_bt_scan_stub_reset();
	g_bt_stub.hci_present = 0;
	g_bt_stub.scan_active = 1;
	g_bt_stub.hci_scanning = 0;
	host_bt_scan_state_sync();
	ASSERT_EQ(g_bt_stub.scan_active, 1);

	TEST_END();
}
