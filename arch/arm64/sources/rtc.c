/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtc.c
 * Description: ARM64 wall-clock provider: the ARM PL031 real-time clock the
 *              devicetree describes (QEMU virt). Boards without one report
 *              -ENODEV and CLOCK_REALTIME stays boot-relative.
 *
 * Reference: ARM PrimeCell RTC (PL031) TRM, DDI 0224; Linux
 * drivers/rtc/rtc-pl031.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arm64_board.h>
#include <ir0/errno.h>
#include <ir0/rtc.h>
#include <stdint.h>

#include "mmu.h"

/* RTCDR: seconds counter, loaded by firmware/QEMU with the host time. */
#define PL031_DR 0x000U

static volatile uint32_t *g_pl031;

int rtc_init(void)
{
	const struct arm64_board_boot_info *info = arm64_board_boot_info();

	if (g_pl031)
		return 0;
	if (!info || info->rtc_mmio.size == 0U)
		return -ENODEV;
	if (arm64_mmu_map_device_range(info->rtc_mmio.base,
				       info->rtc_mmio.size) != 0)
		return -ENODEV;
	g_pl031 = (volatile uint32_t *)(uintptr_t)info->rtc_mmio.base;
	return 0;
}

int rtc_read_time(rtc_time_t *time)
{
	if (!time || !g_pl031)
		return -ENODEV;
	rtc_unix_to_fields((time_t)g_pl031[PL031_DR / 4U], time);
	return 0;
}

uint8_t rtc_read_register(uint8_t reg)
{
	(void)reg;
	return 0xff;
}

void rtc_write_register(uint8_t reg, uint8_t value)
{
	(void)reg;
	(void)value;
}
