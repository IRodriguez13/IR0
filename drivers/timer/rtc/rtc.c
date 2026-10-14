/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtc.c
 * Description: x86 CMOS RTC backend behind <ir0/rtc.h> facade.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "rtc.h"
#include <ir0/rtc.h>
#include <ir0/arch_port.h>
#include <string.h>

/* Wait until CMOS is not mid-update (Status A UIP clear). Bounded spin. */
static void rtc_wait_uip_clear(void)
{
	unsigned spins = 0;

	while ((rtc_read_register(RTC_STATUS_A) & RTC_STATUS_A_UIP) != 0)
	{
		if (++spins > 1000000U)
			break;
	}
}

static void rtc_read_raw(rtc_time_t *time)
{
	time->second = rtc_read_register(RTC_SECONDS);
	time->minute = rtc_read_register(RTC_MINUTES);
	time->hour = rtc_read_register(RTC_HOURS);
	time->day = rtc_read_register(RTC_DAY);
	time->month = rtc_read_register(RTC_MONTH);
	time->year = rtc_read_register(RTC_YEAR);
	time->century = rtc_read_register(RTC_CENTURY);
}

int rtc_init(void)
{
	uint8_t status_b;

	rtc_wait_uip_clear();
	status_b = rtc_read_register(RTC_STATUS_B);
	if (status_b == 0xFF)
		return -1;
	return 0;
}

int rtc_read_time(rtc_time_t *time)
{
	rtc_time_t a;
	rtc_time_t b;
	uint8_t status_b;
	int tries;

	if (!time)
		return -1;

	/*
	 * Read twice after UIP clear; retry while the pair disagrees so we
	 * never publish a torn update across register boundaries.
	 */
	for (tries = 0; tries < 10; tries++)
	{
		rtc_wait_uip_clear();
		rtc_read_raw(&a);
		rtc_wait_uip_clear();
		rtc_read_raw(&b);
		if (a.second == b.second && a.minute == b.minute &&
		    a.hour == b.hour && a.day == b.day && a.month == b.month &&
		    a.year == b.year && a.century == b.century)
			break;
	}

	*time = b;
	status_b = rtc_read_register(RTC_STATUS_B);
	rtc_apply_cmos_format(time, status_b);
	return 0;
}

uint8_t rtc_read_register(uint8_t reg)
{
	outb(RTC_ADDRESS_REG, reg);
	return inb(RTC_DATA_REG);
}

void rtc_write_register(uint8_t reg, uint8_t value)
{
	outb(RTC_ADDRESS_REG, reg);
	outb(RTC_DATA_REG, value);
}
