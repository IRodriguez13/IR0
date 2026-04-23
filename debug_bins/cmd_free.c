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
    char line[128];
    snprintf(line, sizeof(line), "Mem:           %10llu %10llu %10llu\n",
             total, free_kb, used);
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
