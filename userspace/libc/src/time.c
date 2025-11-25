// time.c - Time functions for IR0 libc
#include <time.h>
#include <ir0/syscall.h>

// Static storage for time conversion
static struct tm _tm_storage;

// Get current time in seconds since epoch
time_t time(time_t *tloc) {
    // For now, return a simple counter or syscall if available
    // This would need a kernel syscall to get real time
    time_t t = (time_t)syscall0(SYS_GETPID); // Placeholder - needs real SYS_TIME
    if (tloc)
        *tloc = t;
    return t;
}

// Get processor time used
clock_t clock(void) {
    // Would need kernel support for process CPU time
    // For now, return 0 or a simple counter
    return 0;
}

// Compute difference between two times
double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

// Helper: Check if year is leap year
static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// Convert time_t to struct tm (simplified, assumes UTC)
struct tm *gmtime(const time_t *timer) {
    if (!timer) return NULL;
    
    time_t t = *timer;
    
    // Days since epoch (1970-01-01)
    int days = t / 86400;
    int seconds = t % 86400;
    
    _tm_storage.tm_sec = seconds % 60;
    _tm_storage.tm_min = (seconds / 60) % 60;
    _tm_storage.tm_hour = seconds / 3600;
    
    // Calculate year
    int year = 1970;
    while (1) {
        int days_in_year = is_leap_year(year) ? 366 : 365;
        if (days < days_in_year)
            break;
        days -= days_in_year;
        year++;
    }
    
    _tm_storage.tm_year = year - 1900;
    _tm_storage.tm_yday = days;
    
    // Calculate month and day
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = 0;
    while (month < 12) {
        int dim = days_in_month[month];
        if (month == 1 && is_leap_year(year))
            dim = 29;
        if (days < dim)
            break;
        days -= dim;
        month++;
    }
    
    _tm_storage.tm_mon = month;
    _tm_storage.tm_mday = days + 1;
    
    // Calculate day of week (simplified)
    _tm_storage.tm_wday = 0; // Would need proper calculation
    _tm_storage.tm_isdst = 0;
    
    return &_tm_storage;
}

struct tm *localtime(const time_t *timer) {
    // For now, same as gmtime (no timezone support)
    return gmtime(timer);
}

// Convert struct tm to time_t
time_t mktime(struct tm *timeptr) {
    if (!timeptr) return -1;
    
    // Simplified conversion
    int year = timeptr->tm_year + 1900;
    int days = 0;
    
    // Add days for years
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    
    // Add days for months
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int m = 0; m < timeptr->tm_mon; m++) {
        days += days_in_month[m];
        if (m == 1 && is_leap_year(year))
            days++;
    }
    
    // Add day of month
    days += timeptr->tm_mday - 1;
    
    time_t result = days * 86400;
    result += timeptr->tm_hour * 3600;
    result += timeptr->tm_min * 60;
    result += timeptr->tm_sec;
    
    return result;
}

// Convert struct tm to string
char *asctime(const struct tm *timeptr) {
    static char buf[26];
    if (!timeptr) return NULL;
    
    static const char *wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    // Format: "Wed Jun 30 21:49:08 1993\n"
    // Simplified sprintf-like formatting
    char *p = buf;
    
    // Copy day name
    const char *d = wday[timeptr->tm_wday % 7];
    while (*d) *p++ = *d++;
    *p++ = ' ';
    
    // Copy month name
    const char *m = mon[timeptr->tm_mon % 12];
    while (*m) *p++ = *m++;
    *p++ = ' ';
    
    // Day
    int day = timeptr->tm_mday;
    if (day < 10) *p++ = ' ';
    else *p++ = '0' + (day / 10);
    *p++ = '0' + (day % 10);
    *p++ = ' ';
    
    // Hour
    *p++ = '0' + (timeptr->tm_hour / 10);
    *p++ = '0' + (timeptr->tm_hour % 10);
    *p++ = ':';
    
    // Minute
    *p++ = '0' + (timeptr->tm_min / 10);
    *p++ = '0' + (timeptr->tm_min % 10);
    *p++ = ':';
    
    // Second
    *p++ = '0' + (timeptr->tm_sec / 10);
    *p++ = '0' + (timeptr->tm_sec % 10);
    *p++ = ' ';
    
    // Year
    int year = timeptr->tm_year + 1900;
    *p++ = '0' + (year / 1000);
    *p++ = '0' + ((year / 100) % 10);
    *p++ = '0' + ((year / 10) % 10);
    *p++ = '0' + (year % 10);
    *p++ = '\n';
    *p = '\0';
    
    return buf;
}

char *ctime(const time_t *timer) {
    return asctime(localtime(timer));
}

// Get time of day with microsecond precision
int gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tv) {
        // Would need kernel syscall for real implementation
        tv->tv_sec = time(NULL);
        tv->tv_usec = 0; // No microsecond precision without kernel support
    }
    
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    
    return 0;
}

// Sleep for seconds
unsigned int sleep(unsigned int seconds) {
    // Would need kernel syscall for real sleep
    // For now, busy wait (not ideal)
    volatile unsigned long count = seconds * 1000000;
    while (count--);
    return 0;
}

// Sleep for microseconds
int usleep(unsigned int usec) {
    volatile unsigned long count = usec;
    while (count--);
    return 0;
}
