/* SPDX-License-Identifier: GPL-3.0-only */
/* ARM64 wall-clock provider: unavailable until a board RTC is registered. */

#include <ir0/rtc.h>
#include <ir0/errno.h>

int rtc_init(void)
{
	return -ENODEV;
}

int rtc_read_time(rtc_time_t *time)
{
	(void)time;
	return -ENODEV;
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
