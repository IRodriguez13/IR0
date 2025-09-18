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

// RTC status bits
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
