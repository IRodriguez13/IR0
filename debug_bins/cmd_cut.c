/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - cut command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Minimal cut implementation: cut -d DELIM -f FIELD FILE
 */

#include "debug_bins.h"

static int parse_positive_int(const char *s)
{
    int v = 0;
    if (!s || *s == '\0')
        return -EINVAL;
    while (*s)
    {
        if (*s < '0' || *s > '9')
            return -EINVAL;
        v = v * 10 + (*s - '0');
        s++;
    }
    return (v > 0) ? v : -EINVAL;
}

static int emit_cut_line(const char *line, size_t len, char delim, int field)
{
    int cur_field = 1;
    size_t start = 0;
    size_t end = len;

    for (size_t i = 0; i <= len; i++)
    {
        int at_end = (i == len);
        if (at_end || line[i] == delim)
        {
            if (cur_field == field)
            {
                start = (cur_field == 1) ? 0 : start;
                end = i;
                if (end > start)
                    syscall(SYS_WRITE, 1, (uint64_t)(line + start), (uint64_t)(end - start));
                syscall(SYS_WRITE, 1, (uint64_t)"\n", 1);
                return 0;
            }
            cur_field++;
            start = i + 1;
        }
    }

    syscall(SYS_WRITE, 1, (uint64_t)"\n", 1);
    return 0;
}

static int cmd_cut_handler(int argc, char **argv)
{
    const char *path = NULL;
    char delim = '\t';
    int field = -1;
    int fd;
    char in[1024];
    char line[1024];
    size_t line_len = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
        {
            delim = argv[++i][0];
        }
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc)
        {
            field = parse_positive_int(argv[++i]);
            if (field < 0)
            {
                debug_writeln_err("cut: invalid field number");
                return 1;
            }
        }
        else if (argv[i][0] == '-')
        {
            debug_writeln_err("cut: unknown option");
            return 1;
        }
        else
        {
            path = argv[i];
        }
    }

    if (!path || field < 1)
    {
        debug_writeln_err("usage: cut -d DELIM -f FIELD FILE");
        return 1;
    }

    fd = (int)syscall(SYS_OPEN, (uint64_t)path, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("cut", path, fd);
        return 1;
    }

    for (;;)
    {
        int64_t rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)in, sizeof(in));
        if (rd < 0)
        {
            syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
            debug_perror("cut", path, (int)rd);
            return 1;
        }
        if (rd == 0)
            break;

        for (int64_t i = 0; i < rd; i++)
        {
            char c = in[i];
            if (c == '\n')
            {
                emit_cut_line(line, line_len, delim, field);
                line_len = 0;
            }
            else if (line_len + 1 < sizeof(line))
            {
                line[line_len++] = c;
            }
        }
    }

    if (line_len > 0)
        emit_cut_line(line, line_len, delim, field);

    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    return 0;
}

struct debug_command cmd_cut = {
    .name = "cut",
    .handler = cmd_cut_handler,
    .usage = "cut -d DELIM -f FIELD FILE",
    .description = "Extract one field from delimited lines",
    .flags = NULL
};

