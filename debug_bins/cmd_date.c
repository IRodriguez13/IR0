/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_date.c
 * Description: IR0 kernel source/header file
 */

/*
 * IR0 Kernel - Debug Binary: date
 * Muestra fecha/hora vía gettimeofday (RTC o uptime).
 * Userspace: solo syscall SYS_GETTIMEOFDAY (open/read/write/close no aplican).
 */
#include "debug_bins.h"
#include <ir0/time.h>

/* Días por mes (no bisiesto) */
static const int days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int is_leap(int year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
}

/* Convierte Unix timestamp a YYYY-MM-DD HH:MM:SS en buf */
static void format_timestamp(time_t sec, char *buf, size_t size)
{
    if (!buf || size < 20)
        return;

    int s = (int)(sec % 60);
    sec /= 60;
    int m = (int)(sec % 60);
    sec /= 60;
    int h = (int)(sec % 24);
    sec /= 24;

    /* Días desde 1970 -> año, mes, día */
    int year = 1970;
    while (sec >= (365 + is_leap(year)))
    {
        sec -= (365 + is_leap(year));
        year++;
    }
    int month = 1;
    int dim = days_in_month[month - 1] + (month == 2 ? is_leap(year) : 0);
    while (sec >= dim && month <= 12)
    {
        sec -= dim;
        month++;
        dim = days_in_month[month - 1] + (month == 2 ? is_leap(year) : 0);
    }
    int day = (int)sec + 1;

    snprintf(buf, size, "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, h, m, s);
}

static int cmd_date_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct timeval tv;
    /* Syscall directo, como userspace (libc gettimeofday -> SYS_GETTIMEOFDAY) */
    if (syscall2(SYS_GETTIMEOFDAY, (int64_t)&tv, 0) != 0)
    {
        debug_writeln_err("date: gettimeofday failed");
        debug_serial_fail("date", "gettimeofday");
        return -1;
    }

    char line[80];
    format_timestamp((time_t)tv.tv_sec, line, sizeof(line));
    size_t len = strlen(line);
    snprintf(line + len, sizeof(line) - len, ".%06d UTC\n", (int)tv.tv_usec);
    debug_write(line);
    debug_serial_ok("date");
    return 0;
}

struct debug_command cmd_date = {
    .name = "date",
    .handler = cmd_date_handler,
    .usage = "date",
    .description = "Show current date/time (gettimeofday, RTC)"
};
