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
#if !defined(__x86_64__) && !defined(__i386__)
	/* No PC CMOS on this ISA — wall clock unavailable. */
	return -1;
#else
	uint8_t status_b;

	rtc_wait_uip_clear();
	status_b = rtc_read_register(RTC_STATUS_B);
	if (status_b == 0xFF)
		return -1;
	return 0;
#endif
}

int rtc_read_time(rtc_time_t *time)
{
	rtc_time_t a;
	rtc_time_t b;
	uint8_t status_b;
	int tries;

	if (!time)
		return -1;

#if !defined(__x86_64__) && !defined(__i386__)
	return -1;
#endif

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

void rtc_get_time_string(char *buffer, size_t buffer_size)
{
	rtc_time_t time;

	if (!buffer || buffer_size < 9U)
		return;

	if (rtc_read_time(&time) != 0)
	{
		buffer[0] = '0';
		buffer[1] = '0';
		buffer[2] = ':';
		buffer[3] = '0';
		buffer[4] = '0';
		buffer[5] = ':';
		buffer[6] = '0';
		buffer[7] = '0';
		buffer[8] = '\0';
		return;
	}

	buffer[0] = (char)('0' + (time.hour / 10));
	buffer[1] = (char)('0' + (time.hour % 10));
	buffer[2] = ':';
	buffer[3] = (char)('0' + (time.minute / 10));
	buffer[4] = (char)('0' + (time.minute % 10));
	buffer[5] = ':';
	buffer[6] = (char)('0' + (time.second / 10));
	buffer[7] = (char)('0' + (time.second % 10));
	buffer[8] = '\0';
}

void rtc_get_date_string(char *buffer, size_t buffer_size)
{
	rtc_time_t time;
	uint16_t full_year;

	if (!buffer || buffer_size < 11U)
		return;

	if (rtc_read_time(&time) != 0)
	{
		memcpy(buffer, "01/01/1970", 10);
		buffer[10] = '\0';
		return;
	}

	full_year = (uint16_t)(time.century * 100 + (time.year % 100));
	buffer[0] = (char)('0' + (time.day / 10));
	buffer[1] = (char)('0' + (time.day % 10));
	buffer[2] = '/';
	buffer[3] = (char)('0' + (time.month / 10));
	buffer[4] = (char)('0' + (time.month % 10));
	buffer[5] = '/';
	buffer[6] = (char)('0' + (full_year / 1000));
	buffer[7] = (char)('0' + ((full_year / 100) % 10));
	buffer[8] = (char)('0' + ((full_year / 10) % 10));
	buffer[9] = (char)('0' + (full_year % 10));
	buffer[10] = '\0';
}
