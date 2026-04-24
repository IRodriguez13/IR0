/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_df.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: df
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Show filesystem totals from kernel-exposed runtime data.
 * Uses /proc/mounts + /proc/partitions (syscalls only).
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>

#define DF_BUF_SIZE 4096
#define DF_MAX_PARTS 64

struct df_part
{
    char name[16];
    uint64_t blocks_1k;
};

static int df_read_text_file(const char *path, char *buf, size_t buf_sz)
{
    int fd = ir0_open(path, O_RDONLY, 0);
    if (fd < 0)
        return fd;

    int64_t n = ir0_read(fd, buf, buf_sz - 1);
    ir0_close(fd);
    if (n < 0)
        return (int)n;

    buf[n] = '\0';
    return (int)n;
}

static uint64_t df_parse_u64(const char *s)
{
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (uint64_t)(*s - '0');
        s++;
    }
    return v;
}

static int df_load_partitions(struct df_part *parts, int max_parts)
{
    char buf[DF_BUF_SIZE];
    int nr = df_read_text_file("/proc/partitions", buf, sizeof(buf));
    if (nr < 0)
        return nr;

    int count = 0;
    const char *p = buf;
    while (*p)
    {
        const char *eol = strchr(p, '\n');
        if (!eol)
            break;

        if (eol > p)
        {
            char line[128];
            size_t len = (size_t)(eol - p);
            if (len >= sizeof(line))
                len = sizeof(line) - 1;
            memcpy(line, p, len);
            line[len] = '\0';

            const char *cur = line;
            while (*cur == ' ' || *cur == '\t')
                cur++;

            if (*cur >= '0' && *cur <= '9')
            {
                int field = 0;
                uint64_t blocks = 0;
                char name[16];
                name[0] = '\0';

                while (*cur && field < 4)
                {
                    while (*cur == ' ' || *cur == '\t')
                        cur++;
                    if (*cur == '\0')
                        break;

                    const char *start = cur;
                    while (*cur && *cur != ' ' && *cur != '\t')
                        cur++;
                    size_t flen = (size_t)(cur - start);

                    if (field == 2)
                        blocks = df_parse_u64(start);
                    else if (field == 3)
                    {
                        if (flen >= sizeof(name))
                            flen = sizeof(name) - 1;
                        memcpy(name, start, flen);
                        name[flen] = '\0';
                    }
                    field++;
                }

                if (name[0] != '\0' && count < max_parts)
                {
                    strncpy(parts[count].name, name, sizeof(parts[count].name) - 1);
                    parts[count].name[sizeof(parts[count].name) - 1] = '\0';
                    parts[count].blocks_1k = blocks;
                    count++;
                }
            }
        }
        p = eol + 1;
    }

    return count;
}

static uint64_t df_find_blocks_1k(const struct df_part *parts, int count, const char *dev_path)
{
    const char *name = dev_path;
    if (strncmp(dev_path, "/dev/", 5) == 0)
        name = dev_path + 5;

    for (int i = 0; i < count; i++)
    {
        if (strcmp(parts[i].name, name) == 0)
            return parts[i].blocks_1k;
    }
    return 0;
}

static int cmd_df_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct df_part parts[DF_MAX_PARTS];
    int part_count = df_load_partitions(parts, DF_MAX_PARTS);
    if (part_count < 0)
    {
        debug_perror("df", "/proc/partitions", part_count);
        debug_serial_fail_err("df", "read_partitions", (int)(-part_count));
        return 1;
    }

    char mounts_buf[DF_BUF_SIZE];
    int nr = df_read_text_file("/proc/mounts", mounts_buf, sizeof(mounts_buf));
    if (nr < 0)
    {
        debug_perror("df", "/proc/mounts", nr);
        debug_serial_fail_err("df", "read_mounts", (int)(-nr));
        return 1;
    }

    debug_writeln("Filesystem     1K-blocks    Used Available Use% Mounted on");

    const char *p = mounts_buf;
    while (*p)
    {
        const char *eol = strchr(p, '\n');
        if (!eol)
            break;

        if (eol > p)
        {
            char line[256];
            size_t len = (size_t)(eol - p);
            if (len >= sizeof(line))
                len = sizeof(line) - 1;
            memcpy(line, p, len);
            line[len] = '\0';

            const char *fields[4] = {0};
            int field_count = 0;
            char *cur = line;
            while (*cur && field_count < 4)
            {
                while (*cur == ' ' || *cur == '\t')
                    cur++;
                if (*cur == '\0')
                    break;
                fields[field_count++] = cur;
                while (*cur && *cur != ' ' && *cur != '\t')
                    cur++;
                if (*cur == '\0')
                    break;
                *cur = '\0';
                cur++;
            }

            if (field_count >= 3)
            {
                const char *dev = fields[0];
                const char *mnt = fields[1];
                const char *fstype = fields[2];
                uint64_t total_k = df_find_blocks_1k(parts, part_count, dev);
                char total_txt[32];
                char out[192];

                if (total_k > 0)
                    debug_u64_to_dec(total_k, total_txt, sizeof(total_txt));
                else
                    strncpy(total_txt, "-", sizeof(total_txt) - 1);

                total_txt[sizeof(total_txt) - 1] = '\0';
                snprintf(out, sizeof(out), "%-14s %10s %7s %9s %4s %-10s (%s)",
                         dev, total_txt, "-", "-", "-", mnt, fstype);
                debug_writeln(out);
            }
        }
        p = eol + 1;
    }

    debug_serial_ok("df");
    return 0;
}

struct debug_command cmd_df = {
    .name = "df",
    .handler = cmd_df_handler,
    .usage = "df",
    .description = "Show disk space"
};
