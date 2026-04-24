/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtc.c
 * Description: IR0 kernel source/header file
 */

#include "rtc.h"
#include <ir0/vga.h>
#include <string.h>
#include <stddef.h>
#include <arch/common/arch_interface.h>

/* Wait until CMOS is not mid-update (Status A UIP clear). Bounded spin for broken RTC. */
static void rtc_wait_uip_clear(void)
{
    unsigned spins = 0;

    while ((rtc_read_register(RTC_STATUS_A) & RTC_STATUS_A_UIP) != 0)
    {
        if (++spins > 1000000U)
            break;
    }
}

int rtc_init(void)
{
    rtc_wait_uip_clear();
    /* Check if RTC is available by reading status register B */
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    if (status_b == 0xFF)
    {
        return -1; /* RTC not available */
    }

    return 0;
}

int rtc_read_time(rtc_time_t *time)
{
    if (!time)
    {
        return -1;
    }

    rtc_wait_uip_clear();

    /* Read time registers while not updating */
    time->second = rtc_read_register(RTC_SECONDS);
    time->minute = rtc_read_register(RTC_MINUTES);
    time->hour = rtc_read_register(RTC_HOURS);
    time->day = rtc_read_register(RTC_DAY);
    time->month = rtc_read_register(RTC_MONTH);
    time->year = rtc_read_register(RTC_YEAR);
    time->century = rtc_read_register(RTC_CENTURY);

    /* Check if RTC is in BCD mode */
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    if (!(status_b & RTC_STATUS_B_BINARY))
    {
        /* Convert BCD to binary */
        time->second = rtc_bcd_to_binary(time->second);
        time->minute = rtc_bcd_to_binary(time->minute);
        time->hour = rtc_bcd_to_binary(time->hour);
        time->day = rtc_bcd_to_binary(time->day);
        time->month = rtc_bcd_to_binary(time->month);
        time->year = rtc_bcd_to_binary(time->year);
        time->century = rtc_bcd_to_binary(time->century);
    }

    /* Handle 12-hour format */
    if (!(status_b & RTC_STATUS_B_24HOUR))
    {
        if (time->hour & 0x80)
        {
            time->hour = ((time->hour & 0x7F) + 12) % 24;
        }
    }

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

uint8_t rtc_bcd_to_binary(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

void rtc_get_time_string(char *buffer, size_t buffer_size)
{
    /* HH:MM:SS plus NUL needs 9 bytes */
    if (!buffer || buffer_size < 9U)
    {
        return;
    }

    rtc_time_t time;
    if (rtc_read_time(&time) != 0)
    {
        /* Simple fallback implementation */
        buffer[0] = '0'; buffer[1] = '0'; buffer[2] = ':';
        buffer[3] = '0'; buffer[4] = '0'; buffer[5] = ':';
        buffer[6] = '0'; buffer[7] = '0'; buffer[8] = '\0';
        return;
    }

    /* Simple formatting without snprintf */
    buffer[0] = '0' + (time.hour / 10);
    buffer[1] = '0' + (time.hour % 10);
    buffer[2] = ':';
    buffer[3] = '0' + (time.minute / 10);
    buffer[4] = '0' + (time.minute % 10);
    buffer[5] = ':';
    buffer[6] = '0' + (time.second / 10);
    buffer[7] = '0' + (time.second % 10);
    buffer[8] = '\0';
}

void rtc_get_date_string(char *buffer, size_t buffer_size)
{
    /* DD/MM/YYYY plus NUL needs 11 bytes */
    if (!buffer || buffer_size < 11U)
    {
        return;
    }

    rtc_time_t time;
    if (rtc_read_time(&time) != 0)
    {
        /* Simple fallback implementation */
        buffer[0] = '0'; buffer[1] = '1'; buffer[2] = '/';
        buffer[3] = '0'; buffer[4] = '1'; buffer[5] = '/';
        buffer[6] = '2'; buffer[7] = '0'; buffer[8] = '2'; buffer[9] = '4';
        buffer[10] = '\0';
        return;
    }

    uint16_t full_year = (time.century * 100) + time.year;

    /* Simple formatting without snprintf */
    buffer[0] = '0' + (time.day / 10);
    buffer[1] = '0' + (time.day % 10);
    buffer[2] = '/';
    buffer[3] = '0' + (time.month / 10);
    buffer[4] = '0' + (time.month % 10);
    buffer[5] = '/';
    buffer[6] = '0' + (full_year / 1000);
    buffer[7] = '0' + ((full_year / 100) % 10);
    buffer[8] = '0' + ((full_year / 10) % 10);
    buffer[9] = '0' + (full_year % 10);
    buffer[10] = '\0';
}
