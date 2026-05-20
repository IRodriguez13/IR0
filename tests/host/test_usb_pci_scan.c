/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_usb_pci_scan.c
 * Description: Host test for USB host module when CONFIG_ENABLE_USB_HOST is off.
 */

#include "test_harness.h"
#include <ir0/usb_host.h>

void test_usb_host_disabled_controller_count(void)
{
	int c;

	TEST_BEGIN("usb_host_disabled_controller_count");

	c = ir0_usb_host_controller_count();
	ASSERT_EQ(c, 0);
	ASSERT_EQ(ir0_usb_host_init(), 0);
	c = ir0_usb_host_controller_count();
	ASSERT_EQ(c, 0);

	TEST_END();
}
