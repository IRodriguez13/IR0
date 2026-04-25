/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - cmp command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Compare two files using syscall-only I/O.
 */

#include "debug_bins.h"

static int cmd_cmp_handler(int argc, char **argv)
{
    int fd_a;
    int fd_b;
    uint64_t offset = 0;
    char off_buf[32];
    char line[160];
    char buf_a[1024];
    char buf_b[1024];

    if (argc != 3)
    {
        debug_writeln_err("usage: cmp FILE1 FILE2");
        return 1;
    }

    fd_a = (int)syscall(SYS_OPEN, (uint64_t)argv[1], O_RDONLY, 0);
    if (fd_a < 0)
    {
        debug_perror("cmp", argv[1], fd_a);
        return 1;
    }
    fd_b = (int)syscall(SYS_OPEN, (uint64_t)argv[2], O_RDONLY, 0);
    if (fd_b < 0)
    {
        syscall(SYS_CLOSE, (uint64_t)fd_a, 0, 0);
        debug_perror("cmp", argv[2], fd_b);
        return 1;
    }

    for (;;)
    {
        int64_t rd_a = syscall(SYS_READ, (uint64_t)fd_a, (uint64_t)buf_a, sizeof(buf_a));
        int64_t rd_b = syscall(SYS_READ, (uint64_t)fd_b, (uint64_t)buf_b, sizeof(buf_b));

        if (rd_a < 0 || rd_b < 0)
        {
            syscall(SYS_CLOSE, (uint64_t)fd_a, 0, 0);
            syscall(SYS_CLOSE, (uint64_t)fd_b, 0, 0);
            debug_writeln_err("cmp: read error");
            return 1;
        }

        if (rd_a == 0 && rd_b == 0)
        {
            syscall(SYS_CLOSE, (uint64_t)fd_a, 0, 0);
            syscall(SYS_CLOSE, (uint64_t)fd_b, 0, 0);
            debug_writeln("cmp: files are identical");
            return 0;
        }

        size_t min_rd = ((size_t)rd_a < (size_t)rd_b) ? (size_t)rd_a : (size_t)rd_b;
        for (size_t i = 0; i < min_rd; i++)
        {
            if (buf_a[i] != buf_b[i])
            {
                uint64_t diff_off = offset + (uint64_t)i;
                debug_u64_to_dec(diff_off, off_buf, sizeof(off_buf));
                snprintf(line, sizeof(line), "cmp: differ at byte %s", off_buf);
                syscall(SYS_CLOSE, (uint64_t)fd_a, 0, 0);
                syscall(SYS_CLOSE, (uint64_t)fd_b, 0, 0);
                debug_writeln(line);
                return 1;
            }
        }

        if (rd_a != rd_b)
        {
            uint64_t diff_off = offset + min_rd;
            debug_u64_to_dec(diff_off, off_buf, sizeof(off_buf));
            snprintf(line, sizeof(line), "cmp: files differ in length at byte %s", off_buf);
            syscall(SYS_CLOSE, (uint64_t)fd_a, 0, 0);
            syscall(SYS_CLOSE, (uint64_t)fd_b, 0, 0);
            debug_writeln(line);
            return 1;
        }

        offset += (uint64_t)rd_a;
    }
}

struct debug_command cmd_cmp = {
    .name = "cmp",
    .handler = cmd_cmp_handler,
    .usage = "cmp FILE1 FILE2",
    .description = "Compare two files byte by byte",
    .flags = NULL
};

