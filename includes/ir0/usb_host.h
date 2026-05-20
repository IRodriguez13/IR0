/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: usb_host.h
 * Description: USB host facade for staged driver bootstrap.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * ir0_usb_host_init - PCI scan and host-controller scaffold init.
 *
 * Returns: 0 on success, negative errno on failure.
 */
int ir0_usb_host_init(void);

/*
 * ir0_usb_host_controller_count - Controllers found on last PCI scan.
 */
int ir0_usb_host_controller_count(void);

/*
 * ir0_usb_host_describe - One-line summary for controller @idx from last PCI scan.
 *
 * Writes BDF, vendor:device, PCI programming interface, and BAR0 raw dword.
 *
 * Returns: 0 on success, negative errno on failure.
 */
int ir0_usb_host_describe(int idx, char *buf, size_t len);
