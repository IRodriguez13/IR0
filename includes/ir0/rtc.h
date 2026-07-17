/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — RTC facade (portable API; impl in drivers/timer/rtc/).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

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
void rtc_write_register(uint8_t reg, uint8_t value);
uint8_t rtc_bcd_to_binary(uint8_t bcd);
void rtc_get_time_string(char *buffer, size_t buffer_size);
void rtc_get_date_string(char *buffer, size_t buffer_size);
