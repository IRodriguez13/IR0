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
#include <ir0/clock.h>
#include <ir0/copy_user.h>
#include <ir0/validation.h>

int64_t sys_gettimeofday(struct timeval *tv, void *tz)
{
	uint64_t uptime_ms;
	time_t sec;
	suseconds_t usec;

	(void)tz;

	if (!current_process || !tv)
		return -EFAULT;
	if (validate_userspace_buffer(tv, sizeof(struct timeval)) != 0)
		return -EFAULT;

	uptime_ms = clock_get_uptime_milliseconds();
	if (clock_realtime_available())
	{
		sec = clock_get_current_time();
		/* current_time advances on second boundaries; residual from uptime. */
		usec = (suseconds_t)((uptime_ms % 1000) * 1000);
	}
	else
	{
		/* Honest fallback: no wall clock — boot-relative (same as MONOTONIC). */
		sec = (time_t)(uptime_ms / 1000);
		usec = (suseconds_t)((uptime_ms % 1000) * 1000);
	}
	tv->tv_sec = sec;
	tv->tv_usec = usec;
	return 0;
}

int64_t sys_clock_gettime(int clock_id, struct timespec *tp)
{
	uint64_t uptime_ms;

	if (!current_process || !tp)
		return -EFAULT;

	if (validate_userspace_buffer(tp, sizeof(struct timespec)) != 0)
		return -EFAULT;

	uptime_ms = clock_get_uptime_milliseconds();

	/*
	 * CLOCK_REALTIME (0): RTC_UTC + monotonic elapsed when available.
	 * CLOCK_MONOTONIC (1), MONOTONIC_RAW (4), BOOTTIME (7): uptime since boot.
	 */
	if (clock_id == 0)
	{
		if (clock_realtime_available())
		{
			tp->tv_sec = clock_get_current_time();
			tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
			return 0;
		}
		/* No RTC: still return boot-relative so date(1) is not ENOSYS. */
		tp->tv_sec = (time_t)(uptime_ms / 1000);
		tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
		return 0;
	}

	if (clock_id != 1 && clock_id != 4 && clock_id != 7)
		return -EINVAL;

	tp->tv_sec = (time_t)(uptime_ms / 1000);
	tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
	return 0;
}

/*
 * getitimer/setitimer — honest ENOSYS until SIGALRM delivery exists.
 * TinyX SmartSchedule probes setitimer; on failure it sets
 * SmartScheduleDisable and continues on poll/select (desired).
 * Returning success without timers caused glibc stack-canary abort in X.
 */
int64_t sys_getitimer(int which, struct itimerval *curr_value)
{
	(void)which;
	(void)curr_value;
	return -ENOSYS;
}

int64_t sys_setitimer(int which, const struct itimerval *new_value,
		      struct itimerval *old_value)
{
	(void)which;
	(void)new_value;
	(void)old_value;
	return -ENOSYS;
}
