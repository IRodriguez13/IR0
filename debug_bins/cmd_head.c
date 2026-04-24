/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: cmd_head.c
 * Description: Debug binary: head
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

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

static int cmd_head_handler(int argc, char **argv)
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
            debug_write_err("head: invalid line count\n");
            debug_serial_fail("head", "parse");
            return 1;
        }
        path = argv[3];
    }
    else
    {
        debug_write_err("Usage: head [-n LINES] FILE\n");
        debug_serial_fail("head", "usage");
        return 1;
    }

    int fd = (int)syscall(SYS_OPEN, (uint64_t)path, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("head", path, fd);
        debug_serial_fail_err("head", "open", -fd);
        return 1;
    }

    uint32_t lines = 0;
    char buffer[512];
    while (lines < max_lines)
    {
        int64_t rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buffer, sizeof(buffer));
        if (rd < 0)
        {
            syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
            debug_perror("head", path, (int)rd);
            debug_serial_fail_err("head", "read", (int)(-rd));
            return 1;
        }
        if (rd == 0)
            break;

        for (int64_t i = 0; i < rd && lines < max_lines; i++)
        {
            syscall(SYS_WRITE, STDOUT_FILENO, (uint64_t)&buffer[i], 1);
            if (buffer[i] == '\n')
                lines++;
        }
    }

    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    debug_serial_ok("head");
    return 0;
}

struct debug_command cmd_head = {
    .name = "head",
    .handler = cmd_head_handler,
    .usage = "head [-n LINES] FILE",
    .description = "Print first lines of file"
};
