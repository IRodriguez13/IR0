/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rtc.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

// RTC register addresses
#define RTC_ADDRESS_REG 0x70
#define RTC_DATA_REG    0x71

// RTC register indices
#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_CENTURY     0x32

// RTC status registers
#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B
#define RTC_STATUS_C    0x0C

/* RTC status A: bit 7 = Update In Progress (set while time registers update) */
#define RTC_STATUS_A_UIP    0x80

/* RTC status B bits */
#define RTC_STATUS_B_24HOUR 0x02
#define RTC_STATUS_B_BINARY 0x04

// Time structure
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

// Function declarations
int rtc_init(void);
int rtc_read_time(rtc_time_t *time);
uint8_t rtc_read_register(uint8_t reg);
void rtc_write_register(uint8_t reg, uint8_t value);
uint8_t rtc_bcd_to_binary(uint8_t bcd);
void rtc_get_time_string(char *buffer, size_t buffer_size);
void rtc_get_date_string(char *buffer, size_t buffer_size);
