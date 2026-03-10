/*
 * IR0 Kernel - POSIX time structures (OSDev / Time And Date)
 * struct timespec, struct timeval for nanosleep, gettimeofday
 */
#pragma once

#include <ir0/types.h>

/* POSIX timespec - seconds + nanoseconds */
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

/* POSIX timeval - seconds + microseconds */
typedef long suseconds_t;
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};
