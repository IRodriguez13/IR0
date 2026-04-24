/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: cmd_wc.c
 * Description: Debug binary: wc
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

static int is_space_char(char c)
{
    return (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f');
}

static int cmd_wc_handler(int argc, char **argv)
{
    if (argc != 2)
    {
        debug_write_err("Usage: wc FILE\n");
        debug_serial_fail("wc", "usage");
        return 1;
    }

    const char *path = argv[1];
    int fd = (int)syscall(SYS_OPEN, (uint64_t)path, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("wc", path, fd);
        debug_serial_fail_err("wc", "open", -fd);
        return 1;
    }

    uint64_t lines = 0;
    uint64_t words = 0;
    uint64_t bytes = 0;
    int in_word = 0;
    char buffer[512];

    while (1)
    {
        int64_t rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buffer, sizeof(buffer));
        if (rd < 0)
        {
            syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
            debug_perror("wc", path, (int)rd);
            debug_serial_fail_err("wc", "read", (int)(-rd));
            return 1;
        }
        if (rd == 0)
            break;

        bytes += (uint64_t)rd;
        for (int64_t i = 0; i < rd; i++)
        {
            char c = buffer[i];
            if (c == '\n')
                lines++;

            if (is_space_char(c))
            {
                in_word = 0;
            }
            else if (!in_word)
            {
                words++;
                in_word = 1;
            }
        }
    }

    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);

    char out_lines[32];
    char out_words[32];
    char out_bytes[32];
    char line[192];
    debug_u64_to_dec(lines, out_lines, sizeof(out_lines));
    debug_u64_to_dec(words, out_words, sizeof(out_words));
    debug_u64_to_dec(bytes, out_bytes, sizeof(out_bytes));
    snprintf(line, sizeof(line), "%s %s %s %s\n", out_lines, out_words, out_bytes, path);
    debug_write(line);
    debug_serial_ok("wc");
    return 0;
}

struct debug_command cmd_wc = {
    .name = "wc",
    .handler = cmd_wc_handler,
    .usage = "wc FILE",
    .description = "Count lines, words, and bytes"
};
