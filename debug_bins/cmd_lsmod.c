/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - lsmod command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Dumps /proc/modules for module/driver visibility.
 */

#include "debug_bins.h"

static int cmd_lsmod_handler(int argc, char **argv)
{
    char buf[1024];
    int fd;
    int64_t rd;

    if (argc > 1)
    {
        debug_writeln_err("usage: lsmod");
        return 1;
    }
    (void)argv;

    fd = (int)syscall(SYS_OPEN, (uint64_t)"/proc/modules", O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("lsmod", "/proc/modules", fd);
        return 1;
    }

    rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)(sizeof(buf) - 1));
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (rd < 0)
    {
        debug_perror("lsmod", "/proc/modules", (int)rd);
        return 1;
    }

    buf[(size_t)rd] = '\0';
    debug_write(buf);
    if (rd > 0 && buf[(size_t)rd - 1] != '\n')
        debug_writeln("");
    return 0;
}

struct debug_command cmd_lsmod = {
    .name = "lsmod",
    .handler = cmd_lsmod_handler,
    .usage = "lsmod",
    .description = "List kernel modules/drivers from /proc/modules",
    .flags = NULL
};

