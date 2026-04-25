/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_lshw.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: lshw
 * Copyright (C) 2025 Iván Rodriguez
 *
 * List hardware: reads hostname from /sys, hardware from /proc (raw)
 * and formats. Similar to lshw (warning + hostname + short summary).
 */

#include "debug_bins.h"
#include <string.h>

#define LSHW_BUF_SIZE 2048

static unsigned long long parse_ull(const char *s)
{
    unsigned long long v = 0;
    while (*s >= '0' && *s <= '9')
        { v = v * 10 + (unsigned long long)(*s - '0'); s++; }
    return v;
}

static int read_file(const char *path, char *buf, size_t size)
{
    int fd = syscall(SYS_OPEN, (uint64_t)path, 0, 0);
    if (fd < 0) return -1;
    int nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, size - 1);
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (nr <= 0) return -1;
    buf[nr] = '\0';
    return nr;
}

static void trim_newline(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r'))
        s[--len] = '\0';
}

/* Get value for key from cpuinfo (key\tvalue lines) */
static void cpuinfo_get(const char *buf, const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    const char *p = buf;
    while (*p)
    {
        const char *line_end = strchr(p, '\n');
        if (!line_end) break;
        if (strncmp(p, key, strlen(key)) == 0 && p[strlen(key)] == '\t')
        {
            const char *val = p + strlen(key) + 1;
            size_t n = (size_t)(line_end - val);
            if (n >= out_len) n = out_len - 1;
            memcpy(out, val, n);
            out[n] = '\0';
            return;
        }
        p = line_end + 1;
    }
}

static int cmd_lshw_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    debug_writeln_err("AVISO: debería ejecutar este programa como superusuario.");

    char buf[LSHW_BUF_SIZE];
    if (read_file("/sys/kernel/hostname", buf, sizeof(buf)) > 0)
    {
        trim_newline(buf);
        debug_write(buf);
        debug_write("\n");
    }
    else
        debug_writeln("(hostname)");

    /* Short hardware summary */
    if (read_file("/proc/cpuinfo", buf, sizeof(buf)) > 0)
    {
        char model[128];
        cpuinfo_get(buf, "model name", model, sizeof(model));
        if (model[0])
        {
            debug_write("  CPU: ");
            debug_writeln(model);
        }
    }

    if (read_file("/proc/meminfo", buf, sizeof(buf)) > 0)
    {
        char *p = buf;
        unsigned long long total_kb = parse_ull(p);
        while (*p && *p != '\t') p++;
        if (*p == '\t') p++;
        unsigned long long free_kb = parse_ull(p);
        (void)free_kb;
        char total_kb_str[32];
        char line[80];
        debug_u64_to_dec((uint64_t)total_kb, total_kb_str, sizeof(total_kb_str));
        snprintf(line, sizeof(line), "  Memory: %s kB total\n", total_kb_str);
        debug_write(line);
    }

    if (read_file("/proc/blockdevices", buf, sizeof(buf)) > 0)
    {
        const char *p = buf;
        int first = 1;
        while (*p)
        {
            const char *eol = strchr(p, '\n');
            if (!eol) break;
            if (eol > p)
            {
                /* type name maj min sectors size_human model serial */
                char type[8], name[16], model[64], size_human[16];
                type[0] = name[0] = model[0] = size_human[0] = '\0';
                const char *cur = p;
                for (int f = 0; f < 8 && *cur; f++)
                {
                    const char *start = cur;
                    while (*cur && *cur != '\t' && *cur != '\n') cur++;
                    size_t len = (size_t)(cur - start);
                    if (f == 0) { if (len >= sizeof(type)) len = sizeof(type) - 1; memcpy(type, start, len); type[len] = '\0'; }
                    else if (f == 1) { if (len >= sizeof(name)) len = sizeof(name) - 1; memcpy(name, start, len); name[len] = '\0'; }
                    else if (f == 5) { if (len >= sizeof(size_human)) len = sizeof(size_human) - 1; memcpy(size_human, start, len); size_human[len] = '\0'; }
                    else if (f == 6) { if (len >= sizeof(model)) len = sizeof(model) - 1; memcpy(model, start, len); model[len] = '\0'; }
                    if (*cur == '\t') cur++;
                }
                if (type[0] && strcmp(type, "disk") == 0)
                {
                    if (first) { debug_write("  Disk: "); first = 0; }
                    else debug_write("         ");
                    debug_write(name);
                    if (model[0] && strcmp(model, "-") != 0)
                    {
                        debug_write(" (");
                        debug_write(model);
                        debug_write(")");
                    }
                    if (size_human[0] && strcmp(size_human, "-") != 0)
                    {
                        debug_write(" [");
                        debug_write(size_human);
                        debug_write("]");
                    }
                    debug_write("\n");
                }
            }
            p = eol + 1;
        }
    }

    if (read_file("/proc/netinfo", buf, sizeof(buf)) > 0 && buf[0])
    {
        const char *p = buf;
        int first = 1;
        while (*p)
        {
            const char *eol = strchr(p, '\n');
            if (!eol) break;
            if (eol > p)
            {
                char name[32];
                size_t ni = 0;
                const char *cur = p;
                while (*cur && *cur != '\t' && *cur != '\n' && ni < sizeof(name) - 1)
                    name[ni++] = *cur++;
                name[ni] = '\0';
                if (name[0])
                {
                    if (first) { debug_write("  Network: "); first = 0; }
                    else debug_write("            ");
                    debug_write(name);
                    debug_write("\n");
                }
            }
            p = eol + 1;
        }
    }

    debug_serial_ok("lshw");
    return 0;
}

struct debug_command cmd_lshw = {
    .name = "lshw",
    .handler = cmd_lshw_handler,
    .usage = "lshw",
    .description = "List hardware (hostname, CPU, memory, disk, network)"
};
