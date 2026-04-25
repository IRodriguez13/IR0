/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - tr command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Minimal translate command: tr SET1 SET2 FILE
 */

#include "debug_bins.h"

static char tr_translate_char(char c, const char *set1, const char *set2)
{
    size_t len1 = strlen(set1);
    size_t len2 = strlen(set2);

    for (size_t i = 0; i < len1; i++)
    {
        if (set1[i] == c)
        {
            if (i < len2)
                return set2[i];
            if (len2 > 0)
                return set2[len2 - 1];
            return c;
        }
    }
    return c;
}

static int cmd_tr_handler(int argc, char **argv)
{
    const char *set1;
    const char *set2;
    const char *path;
    int fd;
    char in[1024];

    if (argc != 4)
    {
        debug_writeln_err("usage: tr SET1 SET2 FILE");
        return 1;
    }

    set1 = argv[1];
    set2 = argv[2];
    path = argv[3];
    if (!set1 || !set2 || set1[0] == '\0')
    {
        debug_writeln_err("tr: invalid SET1/SET2");
        return 1;
    }

    fd = (int)syscall(SYS_OPEN, (uint64_t)path, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("tr", path, fd);
        return 1;
    }

    for (;;)
    {
        int64_t rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)in, sizeof(in));
        if (rd < 0)
        {
            syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
            debug_perror("tr", path, (int)rd);
            return 1;
        }
        if (rd == 0)
            break;

        for (int64_t i = 0; i < rd; i++)
            in[i] = tr_translate_char(in[i], set1, set2);

        (void)syscall(SYS_WRITE, 1, (uint64_t)in, (uint64_t)rd);
    }

    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    return 0;
}

struct debug_command cmd_tr = {
    .name = "tr",
    .handler = cmd_tr_handler,
    .usage = "tr SET1 SET2 FILE",
    .description = "Translate characters from SET1 to SET2",
    .flags = NULL
};

