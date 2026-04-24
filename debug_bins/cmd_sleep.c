/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: cmd_sleep.c
 * Description: Debug binary: sleep
 */

#include "debug_bins.h"

static int parse_seconds(const char *s, uint64_t *out)
{
    uint64_t value = 0;

    if (!s || !out || s[0] == '\0')
        return -1;

    while (*s)
    {
        if (*s < '0' || *s > '9')
            return -1;

        uint64_t digit = (uint64_t)(*s - '0');
        if (value > (UINT64_MAX - digit) / 10ULL)
            return -1;

        value = value * 10ULL + digit;
        s++;
    }

    *out = value;
    return 0;
}

static int cmd_sleep_handler(int argc, char **argv)
{
    if (argc != 2)
    {
        debug_write_err("Usage: sleep SECONDS\n");
        debug_serial_fail("sleep", "usage");
        return 1;
    }

    uint64_t seconds = 0;
    if (parse_seconds(argv[1], &seconds) != 0)
    {
        debug_write_err("sleep: invalid number\n");
        debug_serial_fail("sleep", "parse");
        return 1;
    }

    struct timespec req;
    req.tv_sec = (time_t)seconds;
    req.tv_nsec = 0;

    int64_t ret = syscall(SYS_NANOSLEEP, (uint64_t)&req, 0, 0);
    if (ret < 0)
    {
        debug_perror("sleep", argv[1], (int)ret);
        debug_serial_fail_err("sleep", "nanosleep", (int)(-ret));
        return 1;
    }

    debug_serial_ok("sleep");
    return 0;
}

struct debug_command cmd_sleep = {
    .name = "sleep",
    .handler = cmd_sleep_handler,
    .usage = "sleep SECONDS",
    .description = "Sleep for N seconds"
};
