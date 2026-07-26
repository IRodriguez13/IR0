/**
 * Host tests — RTC calendar / BCD (no CMOS I/O).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/rtc.h>
#include <string.h>

void test_rtc_calendar(void)
{
	rtc_time_t t;
	time_t unix_t;

	TEST_BEGIN("rtc_calendar");

	ASSERT_EQ(rtc_bcd_to_binary(0x23), 23);
	ASSERT_EQ(rtc_bcd_to_binary(0x59), 59);

	memset(&t, 0, sizeof(t));
	t.century = 20;
	t.year = 26;
	t.month = 7;
	t.day = 26;
	t.hour = 12;
	t.minute = 0;
	t.second = 0;
	ASSERT_EQ(rtc_civil_year(&t), 2026);
	unix_t = rtc_fields_to_unix(&t);
	/* 2026-07-26 12:00:00 UTC ≈ 1785067200 */
	ASSERT_EQ((unix_t > 1700000000) ? 1 : 0, 1);
	ASSERT_EQ((unix_t < 1900000000) ? 1 : 0, 1);

	/* Full year in .year with century=0 (same as date-string path). */
	memset(&t, 0, sizeof(t));
	t.year = 2026;
	t.month = 1;
	t.day = 1;
	ASSERT_EQ(rtc_civil_year(&t), 2026);

	/* Leap day 2024-02-29 */
	memset(&t, 0, sizeof(t));
	t.century = 20;
	t.year = 24;
	t.month = 2;
	t.day = 29;
	t.hour = 0;
	t.minute = 0;
	t.second = 0;
	unix_t = rtc_fields_to_unix(&t);
	ASSERT_EQ((unix_t > 1700000000) ? 1 : 0, 1);

	/* BCD + 12h PM → 13:00 */
	memset(&t, 0, sizeof(t));
	t.second = 0x00;
	t.minute = 0x00;
	t.hour = 0x81; /* PM + BCD 1 */
	t.day = 0x01;
	t.month = 0x01;
	t.year = 0x70;
	t.century = 0x19;
	rtc_apply_cmos_format(&t, 0); /* BCD, 12-hour */
	ASSERT_EQ(t.hour, 13);
	ASSERT_EQ(t.year, 70);
	ASSERT_EQ(t.century, 19);

	TEST_END();
}
