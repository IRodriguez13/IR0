/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: time_syscalls.c
 * Description: time syscalls (gettimeofday, clock_gettime)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "time_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/syscalls_kernel.h>
#include <ir0/process.h>
#include <ir0/errno.h>
#include <ir0/rtc.h>
#include <ir0/clock.h>

/*
 * rtc_time_to_unix - Convert RTC date/time to Unix timestamp (seconds since 1970-01-01 UTC).
 * Simplified: assumes UTC, no leap seconds.
 */
static time_t rtc_time_to_unix(const rtc_time_t *rt)
{
	uint16_t year = (rt->century > 0 && rt->century < 100) ?
	    (uint16_t)(rt->century * 100 + rt->year) : (uint16_t)(2000 + rt->year);
	int leap;
	time_t days = 0;

	if (year < 1970)
		year = 1970;

	static const int days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

	leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;

	for (uint16_t y = 1970; y < year; y++)
		days += 365 + ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0);

	for (int m = 1; m < (int)rt->month && m <= 12; m++)
		days += days_in_month[m - 1] + (m == 2 ? leap : 0);

	days += (rt->day > 0 && rt->day <= 31) ? (rt->day - 1) : 0;

	return (time_t)days * 86400 +
	    (rt->hour < 24 ? rt->hour : 0) * 3600 +
	    (rt->minute < 60 ? rt->minute : 0) * 60 +
	    (rt->second < 60 ? rt->second : 0);
}

int64_t sys_gettimeofday(struct timeval *tv, void *tz)
{
	uint64_t uptime_ms;

	(void)tz;

	if (!current_process || !tv)
		return -EFAULT;
	if (validate_userspace_buffer(tv, sizeof(struct timeval)) != 0)
		return -EFAULT;

	uptime_ms = clock_get_uptime_milliseconds();
	tv->tv_sec = (time_t)(uptime_ms / 1000);
	tv->tv_usec = (suseconds_t)((uptime_ms % 1000) * 1000);
	return 0;
}

int64_t sys_clock_gettime(int clock_id, struct timespec *tp)
{
	uint64_t uptime_ms;

	if (!current_process || !tp)
		return -EFAULT;

	if (validate_userspace_buffer(tp, sizeof(struct timespec)) != 0)
		return -EFAULT;

	if (clock_id != 0 && clock_id != 1)
		return -EINVAL;

	uptime_ms = clock_get_uptime_milliseconds();
	tp->tv_sec = (time_t)(uptime_ms / 1000);
	tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
	return 0;
}
