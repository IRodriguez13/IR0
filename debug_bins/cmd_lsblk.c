/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: lsblk
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Frontend: reads raw data from /proc/blockdevices and formats for display.
 * Endpoint only serves data; this binary does all presentation.
 */

#include "debug_bins.h"

static unsigned long long parse_ull(const char *s)
{
    unsigned long long v = 0;
    while (*s && (*s < '0' || *s > '9'))
        s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned long long)(*s - '0'); s++; }
    return v;
}

#define BLK_BUF_SIZE  4096
#define MAX_PARTS     16

struct blk_line {
    char type[8];
    char name[16];
    unsigned maj, min;
    unsigned long long sectors;
    char model[64];
    char serial[32];
};

static void format_size(unsigned long long sectors, char *out, size_t out_len)
{
    unsigned long long bytes = sectors * 512;
    unsigned long long mb = bytes / (1024 * 1024);
    unsigned long long gb = mb / 1024;
    char size_num[32];

    if (gb > 0)
    {
        debug_u64_to_dec((uint64_t)gb, size_num, sizeof(size_num));
        snprintf(out, out_len, "%sG", size_num);
    }
    else
    {
        debug_u64_to_dec((uint64_t)mb, size_num, sizeof(size_num));
        snprintf(out, out_len, "%sM", size_num);
    }
}

/* Parse one line; fields are tab-separated (type name maj min sectors model serial) */
static int parse_line(const char *line, struct blk_line *r)
{
    const char *p = line;
    r->type[0] = r->name[0] = r->model[0] = r->serial[0] = '\0';
    r->maj = r->min = 0;
    r->sectors = 0;
    for (int f = 0; f < 7 && *p; f++)
    {
        const char *start = p;
        while (*p && *p != '\t' && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) return -1;
        switch (f)
        {
        case 0: len = len < sizeof(r->type) ? len : sizeof(r->type) - 1; memcpy(r->type, start, len); r->type[len] = '\0'; break;
        case 1: len = len < sizeof(r->name) ? len : sizeof(r->name) - 1; memcpy(r->name, start, len); r->name[len] = '\0'; break;
        case 2: r->maj = (unsigned)atoi(start); break;
        case 3: r->min = (unsigned)atoi(start); break;
        case 4: r->sectors = parse_ull(start); break;
        case 5: len = len < sizeof(r->model) ? len : sizeof(r->model) - 1; memcpy(r->model, start, len); r->model[len] = '\0'; break;
        case 6: len = len < sizeof(r->serial) ? len : sizeof(r->serial) - 1; memcpy(r->serial, start, len); r->serial[len] = '\0'; break;
        }
        if (*p == '\t') p++;
    }
    return (r->type[0] && r->name[0]) ? 0 : -1;
}

static int cmd_lsblk_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int fd = syscall(SYS_OPEN, (uint64_t)"/proc/blockdevices", 0, 0);
    if (fd < 0)
    {
        debug_writeln_err("lsblk: cannot open /proc/blockdevices");
        debug_serial_fail("lsblk", "open");
        return -1;
    }

    char buf[BLK_BUF_SIZE];
    int nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, sizeof(buf) - 1);
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (nr <= 0)
    {
        debug_writeln("NAME      TYPE  MAJ:MIN  SIZE   MODEL");
        debug_serial_ok("lsblk");
        return 0;
    }
    buf[nr] = '\0';

    /* Header */
    debug_writeln("NAME      TYPE  MAJ:MIN  SIZE   MODEL");
    debug_writeln("---------------------------------------------------------------");

    char size_str[16];
    const char *p = buf;
    while (*p)
    {
        const char *eol = strchr(p, '\n');
        if (!eol) break;
        if (eol > p)
        {
            struct blk_line line;
            char line_buf[256];
            size_t len = (size_t)(eol - p);
            if (len >= sizeof(line_buf)) len = sizeof(line_buf) - 1;
            memcpy(line_buf, p, len);
            line_buf[len] = '\0';
            if (parse_line(line_buf, &line) == 0)
            {
                format_size(line.sectors, size_str, sizeof(size_str));
                if (strcmp(line.type, "disk") == 0)
                {
                    char out[160];
                    if (line.serial[0] && strcmp(line.serial, "-") != 0)
                    {
                        snprintf(out, sizeof(out), "%-9s %-5s %3u:%-3u  %6s  %s (%s)",
                                 line.name, line.type, line.maj, line.min, size_str, line.model, line.serial);
                    }
                    else
                    {
                        snprintf(out, sizeof(out), "%-9s %-5s %3u:%-3u  %6s  %s",
                                 line.name, line.type, line.maj, line.min, size_str, line.model);
                    }
                    debug_writeln(out);
                }
                else
                {
                    char out[120];
                    snprintf(out, sizeof(out), "  %-7s %-5s %3u:%-3u  %6s",
                             line.name, line.type, line.maj, line.min, size_str);
                    debug_writeln(out);
                }
            }
        }
        p = eol + 1;
    }
    debug_serial_ok("lsblk");
    return 0;
}

struct debug_command cmd_lsblk = {
    .name = "lsblk",
    .handler = cmd_lsblk_handler,
    .usage = "lsblk",
    .description = "List block devices"
};
