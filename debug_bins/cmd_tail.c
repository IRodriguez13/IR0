/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: cmd_tail.c
 * Description: Debug binary: tail
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

#define TAIL_BUFFER_CAP 16384

static int parse_count(const char *s, uint32_t *out)
{
    uint32_t v = 0;

    if (!s || !out || s[0] == '\0')
        return -1;

    while (*s)
    {
        if (*s < '0' || *s > '9')
            return -1;
        uint32_t digit = (uint32_t)(*s - '0');
        if (v > (UINT32_MAX - digit) / 10U)
            return -1;
        v = v * 10U + digit;
        s++;
    }

    *out = v;
    return 0;
}

static int cmd_tail_handler(int argc, char **argv)
{
    uint32_t max_lines = 10;
    const char *path = NULL;

    if (argc == 2)
    {
        path = argv[1];
    }
    else if (argc == 4 && strcmp(argv[1], "-n") == 0)
    {
        if (parse_count(argv[2], &max_lines) != 0)
        {
            debug_write_err("tail: invalid line count\n");
            debug_serial_fail("tail", "parse");
            return 1;
        }
        path = argv[3];
    }
    else
    {
        debug_write_err("Usage: tail [-n LINES] FILE\n");
        debug_serial_fail("tail", "usage");
        return 1;
    }

    int fd = (int)syscall(SYS_OPEN, (uint64_t)path, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("tail", path, fd);
        debug_serial_fail_err("tail", "open", -fd);
        return 1;
    }

    char ring[TAIL_BUFFER_CAP];
    size_t used = 0;
    char chunk[512];
    while (1)
    {
        int64_t rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)chunk, sizeof(chunk));
        if (rd < 0)
        {
            syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
            debug_perror("tail", path, (int)rd);
            debug_serial_fail_err("tail", "read", (int)(-rd));
            return 1;
        }
        if (rd == 0)
            break;

        if ((size_t)rd >= sizeof(ring))
        {
            memcpy(ring, chunk + (size_t)rd - sizeof(ring), sizeof(ring));
            used = sizeof(ring);
            continue;
        }

        if (used + (size_t)rd > sizeof(ring))
        {
            size_t drop = (used + (size_t)rd) - sizeof(ring);
            memmove(ring, ring + drop, used - drop);
            used -= drop;
        }
        memcpy(ring + used, chunk, (size_t)rd);
        used += (size_t)rd;
    }
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);

    if (max_lines == 0)
    {
        debug_serial_ok("tail");
        return 0;
    }

    size_t start = 0;
    uint32_t seen = (used > 0 && ring[used - 1] != '\n') ? 1U : 0U;
    for (size_t i = used; i > 0; i--)
    {
        if (ring[i - 1] == '\n')
        {
            seen++;
            if (seen > max_lines)
            {
                start = i;
                break;
            }
        }
    }

    if (used > start)
        syscall(SYS_WRITE, STDOUT_FILENO, (uint64_t)(ring + start), used - start);

    debug_serial_ok("tail");
    return 0;
}

struct debug_command cmd_tail = {
    .name = "tail",
    .handler = cmd_tail_handler,
    .usage = "tail [-n LINES] FILE",
    .description = "Print last lines of file"
};
