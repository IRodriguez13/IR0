/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: uptime
 * Frontend: reads raw /proc/uptime (uptime_sec) and formats.
 */

#include "debug_bins.h"
#include <stdlib.h>

#define BUF_SIZE 64

static unsigned long long parse_ull(const char *s)
{
    unsigned long long v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned long long)(*s - '0'); s++; }
    return v;
}

static int cmd_uptime_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int fd = syscall(SYS_OPEN, (uint64_t)"/proc/uptime", 0, 0);
    if (fd < 0)
    {
        debug_writeln_err("uptime: cannot open /proc/uptime");
        return -1;
    }
    char buf[BUF_SIZE];
    int nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, sizeof(buf) - 1);
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (nr <= 0)
    {
        debug_writeln(" 0:00:00 up 0 min");
        return 0;
    }
    buf[nr] = '\0';
    unsigned long long sec = parse_ull(buf);
    unsigned long h = (unsigned long)(sec / 3600);
    unsigned long m = (unsigned long)((sec % 3600) / 60);
    unsigned long s = (unsigned long)(sec % 60);
    char line[80];
    snprintf(line, sizeof(line), " %lu:%02lu:%02lu up %lu min\n", h, m, s, (unsigned long)(sec / 60));
    debug_write(line);
    return 0;
}

struct debug_command cmd_uptime = {
    .name = "uptime",
    .handler = cmd_uptime_handler,
    .usage = "uptime",
    .description = "Show system uptime"
};
