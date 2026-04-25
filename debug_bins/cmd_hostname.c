/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - hostname command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Read or set system hostname through /sys/kernel/hostname.
 */

#include "debug_bins.h"

static int cmd_hostname_handler(int argc, char **argv)
{
    int fd;
    int64_t n;
    char buf[128];

    if (argc == 1)
    {
        fd = (int)syscall(SYS_OPEN, (uint64_t)"/sys/kernel/hostname", O_RDONLY, 0);
        if (fd < 0)
        {
            debug_perror("hostname", "/sys/kernel/hostname", fd);
            return 1;
        }

        n = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)(sizeof(buf) - 1));
        syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
        if (n < 0)
        {
            debug_perror("hostname", "/sys/kernel/hostname", (int)n);
            return 1;
        }

        buf[(size_t)n] = '\0';
        debug_write(buf);
        if (n > 0 && buf[(size_t)n - 1] != '\n')
            debug_writeln("");
        return 0;
    }

    if (argc == 2)
    {
        fd = (int)syscall(SYS_OPEN, (uint64_t)"/sys/kernel/hostname", O_WRONLY, 0);
        if (fd < 0)
        {
            debug_perror("hostname", "/sys/kernel/hostname", fd);
            return 1;
        }

        n = syscall(SYS_WRITE, (uint64_t)fd, (uint64_t)argv[1], (uint64_t)strlen(argv[1]));
        syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
        if (n < 0)
        {
            debug_perror("hostname", "/sys/kernel/hostname", (int)n);
            return 1;
        }
        return 0;
    }

    debug_writeln_err("usage: hostname [NEW_NAME]");
    return 1;
}

struct debug_command cmd_hostname = {
    .name = "hostname",
    .handler = cmd_hostname_handler,
    .usage = "hostname [NEW_NAME]",
    .description = "Read or set hostname via /sys/kernel/hostname",
    .flags = NULL
};

