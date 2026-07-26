/**
 * IR0 Kernel — CMOS RTC register map (x86 backend private).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/rtc.h>

#define RTC_ADDRESS_REG 0x70
#define RTC_DATA_REG    0x71

#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_CENTURY     0x32

#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B
#define RTC_STATUS_C    0x0C

#define RTC_STATUS_A_UIP    0x80
