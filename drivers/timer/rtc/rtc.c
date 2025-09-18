#include "rtc.h"
#include <ir0/print.h>
#include <string.h>
#include <stddef.h>
#include <arch/common/arch_interface.h>

// Using I/O functions from arch_interface.h

int rtc_init(void) 
{
    // Check if RTC is available by reading status register B
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    if (status_b == 0xFF) 
    {
        return -1; // RTC not available
    }
    
    return 0;
}

int rtc_read_time(rtc_time_t *time) 
{
    if (!time) 
    {
        return -1;
    }
    
    // Read time registers
    time->second = rtc_read_register(RTC_SECONDS);
    time->minute = rtc_read_register(RTC_MINUTES);
    time->hour = rtc_read_register(RTC_HOURS);
    time->day = rtc_read_register(RTC_DAY);
    time->month = rtc_read_register(RTC_MONTH);
    time->year = rtc_read_register(RTC_YEAR);
    time->century = rtc_read_register(RTC_CENTURY);
    
    // Check if RTC is in BCD mode
    uint8_t status_b = rtc_read_register(RTC_STATUS_B);
    if (!(status_b & RTC_STATUS_B_BINARY)) 
    {
        // Convert BCD to binary
        time->second = rtc_bcd_to_binary(time->second);
        time->minute = rtc_bcd_to_binary(time->minute);
        time->hour = rtc_bcd_to_binary(time->hour);
        time->day = rtc_bcd_to_binary(time->day);
        time->month = rtc_bcd_to_binary(time->month);
        time->year = rtc_bcd_to_binary(time->year);
        time->century = rtc_bcd_to_binary(time->century);
    }
    
    // Handle 12-hour format
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
    (void)buffer_size; // Parameter not used in this implementation
    rtc_time_t time;
    if (rtc_read_time(&time) != 0) 
    {
        // Simple fallback implementation
        buffer[0] = '0'; buffer[1] = '0'; buffer[2] = ':';
        buffer[3] = '0'; buffer[4] = '0'; buffer[5] = ':';
        buffer[6] = '0'; buffer[7] = '0'; buffer[8] = '\0';
        return;
    }
    
    // Simple formatting without snprintf
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
    (void)buffer_size; // Parameter not used in this implementation
    rtc_time_t time;
    if (rtc_read_time(&time) != 0) 
    {
        // Simple fallback implementation
        buffer[0] = '0'; buffer[1] = '1'; buffer[2] = '/';
        buffer[3] = '0'; buffer[4] = '1'; buffer[5] = '/';
        buffer[6] = '2'; buffer[7] = '0'; buffer[8] = '2'; buffer[9] = '4';
        buffer[10] = '\0';
        return;
    }
    
    uint16_t full_year = (time.century * 100) + time.year;
    
    // Simple formatting without snprintf
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
