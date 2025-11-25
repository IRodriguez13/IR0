#pragma once

#include <stddef.h>
#include <stdint.h>

// Time types
typedef int64_t time_t;  // Match kernel definition
typedef long clock_t;
typedef long suseconds_t;

#define CLOCKS_PER_SEC 1000000L

// Time structure
struct tm {
    int tm_sec;    // Seconds [0,60]
    int tm_min;    // Minutes [0,59]
    int tm_hour;   // Hour [0,23]
    int tm_mday;   // Day of month [1,31]
    int tm_mon;    // Month of year [0,11]
    int tm_year;   // Years since 1900
    int tm_wday;   // Day of week [0,6] (Sunday = 0)
    int tm_yday;   // Day of year [0,365]
    int tm_isdst;  // Daylight Saving Time flag
};

// Timeval structure (for gettimeofday)
struct timeval {
    time_t tv_sec;        // Seconds
    suseconds_t tv_usec;  // Microseconds
};

struct timezone {
    int tz_minuteswest;   // Minutes west of GMT
    int tz_dsttime;       // Type of DST correction
};

// Time functions
time_t time(time_t *tloc);
clock_t clock(void);
double difftime(time_t time1, time_t time0);

// Time conversion
struct tm *localtime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
time_t mktime(struct tm *timeptr);
char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);

// High-resolution time
int gettimeofday(struct timeval *tv, struct timezone *tz);

// Sleep functions
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
