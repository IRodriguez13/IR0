/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtc_calendar.c
 * Description: Pure calendar helpers for RTC → Unix time (no I/O).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/rtc.h>
#include <string.h>

uint8_t rtc_bcd_to_binary(uint8_t bcd)
{
	return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

uint16_t rtc_civil_year(const rtc_time_t *rt)
{
	uint16_t year;

	if (!rt)
		return 1970;

	if (rt->century > 0 && rt->century < 100)
		year = (uint16_t)(rt->century * 100 + (rt->year % 100));
	else if (rt->year >= 1970)
		year = rt->year;
	else
		year = (uint16_t)(2000 + (rt->year % 100));

	if (year < 1970)
		year = 1970;
	return year;
}

/*
 * Convert civil date/time (UTC) to seconds since 1970-01-01.
 * Leap years: Gregorian. No leap seconds.
 */
time_t rtc_fields_to_unix(const rtc_time_t *rt)
{
	uint16_t year;
	int leap;
	time_t days = 0;
	static const int days_in_month[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

	if (!rt)
		return 0;

	year = rtc_civil_year(rt);

	leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;

	for (uint16_t y = 1970; y < year; y++)
		days += 365 + ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0);

	for (int m = 1; m < (int)rt->month && m <= 12; m++)
		days += days_in_month[m - 1] + (m == 2 ? leap : 0);

	if (rt->day > 0 && rt->day <= 31)
		days += (rt->day - 1);

	return (time_t)days * 86400 +
	       (rt->hour < 24 ? rt->hour : 0) * 3600 +
	       (rt->minute < 60 ? rt->minute : 0) * 60 +
	       (rt->second < 60 ? rt->second : 0);
}

/*
 * Convert seconds since 1970-01-01 UTC to civil fields, for RTCs that count
 * seconds (ARM PL031). Inverse of rtc_fields_to_unix(); days to civil date
 * after H. Hinnant's days_from_civil algorithm.
 */
void rtc_unix_to_fields(time_t t, rtc_time_t *rt)
{
	int64_t days;
	int64_t secs;
	int64_t era;
	int64_t doe;
	int64_t yoe;
	int64_t doy;
	int64_t mp;
	int64_t year;

	if (!rt)
		return;
	if (t < 0)
		t = 0;
	days = (int64_t)t / 86400;
	secs = (int64_t)t % 86400;

	days += 719468; /* shift the epoch to 0000-03-01 */
	era = days / 146097;
	doe = days - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	year = yoe + era * 400;

	memset(rt, 0, sizeof(*rt));
	rt->day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
	rt->month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
	rt->year = (uint16_t)(year + (rt->month <= 2 ? 1 : 0));
	rt->hour = (uint8_t)(secs / 3600);
	rt->minute = (uint8_t)((secs % 3600) / 60);
	rt->second = (uint8_t)(secs % 60);
}

void rtc_apply_cmos_format(rtc_time_t *time, uint8_t status_b)
{
	uint8_t hour_raw;

	if (!time)
		return;

	hour_raw = time->hour;

	if (!(status_b & RTC_STATUS_B_BINARY))
	{
		time->second = rtc_bcd_to_binary(time->second);
		time->minute = rtc_bcd_to_binary(time->minute);
		time->day = rtc_bcd_to_binary(time->day);
		time->month = rtc_bcd_to_binary(time->month);
		time->year = rtc_bcd_to_binary((uint8_t)(time->year & 0xFF));
		time->century = rtc_bcd_to_binary(time->century);
		/* Hour: strip PM bit before BCD decode when in 12h mode. */
		if (!(status_b & RTC_STATUS_B_24HOUR))
			time->hour = rtc_bcd_to_binary((uint8_t)(hour_raw & 0x7F));
		else
			time->hour = rtc_bcd_to_binary(hour_raw);
	}
	else if (!(status_b & RTC_STATUS_B_24HOUR))
	{
		time->hour = (uint8_t)(hour_raw & 0x7F);
	}

	if (!(status_b & RTC_STATUS_B_24HOUR))
	{
		/* CMOS: bit 7 of raw hour = PM in 12-hour mode. */
		if (hour_raw & 0x80)
		{
			if (time->hour < 12)
				time->hour = (uint8_t)(time->hour + 12);
		}
		else if (time->hour == 12)
		{
			time->hour = 0;
		}
	}
}

