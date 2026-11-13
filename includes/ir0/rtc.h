/**
 * IR0 Kernel — RTC facade (portable API; CMOS I/O in drivers/timer/rtc/).
 *
 * File: rtc.h
 * Description: Wall-clock RTC read + pure calendar helpers (UTC).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#ifdef TEST_HOST
#include <time.h>
#else
#include <ir0/types.h>
#endif

/* Status B bits (CMOS); used by calendar normalize helpers. */
#define RTC_STATUS_B_24HOUR 0x02
#define RTC_STATUS_B_BINARY 0x04

typedef struct
{
	uint8_t second;
	uint8_t minute;
	uint8_t hour;
	uint8_t day;
	uint8_t month;
	uint16_t year;
	uint8_t century;
} rtc_time_t;

int rtc_init(void);
int rtc_read_time(rtc_time_t *time);
uint8_t rtc_read_register(uint8_t reg);

uint8_t rtc_bcd_to_binary(uint8_t bcd);
/* Resolve century + year (or full year) to a Gregorian year ≥ 1970. */
uint16_t rtc_civil_year(const rtc_time_t *rt);
time_t rtc_fields_to_unix(const rtc_time_t *rt);
/* Seconds since the epoch (UTC) to civil fields with a full .year. */
void rtc_unix_to_fields(time_t t, rtc_time_t *rt);
void rtc_apply_cmos_format(rtc_time_t *time, uint8_t status_b);
