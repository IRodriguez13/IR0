/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_free.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: free
 * Frontend: reads raw /proc/meminfo (total_kb\tfree_kb\tused_kb) and formats.
 */

#include "debug_bins.h"

#define BUF_SIZE 256

static unsigned long long parse_ull(const char *s)
{
    unsigned long long v = 0;
    while (*s && (*s < '0' || *s > '9'))
        s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned long long)(*s - '0'); s++; }
    return v;
}

static int cmd_free_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int fd = syscall(SYS_OPEN, (uint64_t)"/proc/meminfo", 0, 0);
    if (fd < 0)
    {
        debug_writeln_err("free: cannot open /proc/meminfo");
        debug_serial_fail("free", "open");
        return -1;
    }
    char buf[BUF_SIZE];
    int nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, sizeof(buf) - 1);
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (nr <= 0)
    {
        debug_writeln("              total        free        used");
        return 0;
    }
    buf[nr] = '\0';
    unsigned long long total = 0, free_kb = 0, used = 0;
    char *p = buf;
    total = parse_ull(p);
    while (*p && *p != '\t') p++;
    if (*p == '\t') p++;
    free_kb = parse_ull(p);
    while (*p && *p != '\t') p++;
    if (*p == '\t') p++;
    used = parse_ull(p);
    char total_str[32];
    char free_str[32];
    char used_str[32];
    char line[128];
    debug_u64_to_dec((uint64_t)total, total_str, sizeof(total_str));
    debug_u64_to_dec((uint64_t)free_kb, free_str, sizeof(free_str));
    debug_u64_to_dec((uint64_t)used, used_str, sizeof(used_str));
    debug_writeln("              total(kB)    free(kB)    used(kB)");
    snprintf(line, sizeof(line), "Mem:           %10s %10s %10s\n",
             total_str, free_str, used_str);
    debug_write(line);
    debug_serial_ok("free");
    return 0;
}

struct debug_command cmd_free = {
    .name = "free",
    .handler = cmd_free_handler,
    .usage = "free",
    .description = "Display memory usage"
};
