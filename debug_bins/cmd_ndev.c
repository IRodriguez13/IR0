/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - ndev command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Dump network device state from /dev/net (devfs view).
 */

#include "debug_bins.h"

static int cmd_ndev_handler(int argc, char **argv)
{
    int fd;
    int64_t rd;
    char buf[2048];

    if (argc > 1)
    {
        debug_writeln_err("usage: ndev");
        return 1;
    }
    (void)argv;

    fd = (int)syscall(SYS_OPEN, (uint64_t)"/dev/net", O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("ndev", "/dev/net", fd);
        return 1;
    }

    rd = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)(sizeof(buf) - 1));
    syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (rd < 0)
    {
        debug_perror("ndev", "/dev/net", (int)rd);
        return 1;
    }

    buf[(size_t)rd] = '\0';
    if (rd == 0 || buf[0] == '\0')
    {
        debug_writeln("ndev: /dev/net returned no data");
        return 0;
    }
    if (strstr(buf, "success:") != NULL)
        debug_writeln("ndev: last ping result");
    else
        debug_writeln("ndev: network device snapshot");
    debug_write(buf);
    if (rd > 0 && buf[(size_t)rd - 1] != '\n')
        debug_writeln("");
    return 0;
}

struct debug_command cmd_ndev = {
    .name = "ndev",
    .handler = cmd_ndev_handler,
    .usage = "ndev",
    .description = "Dump /dev/net state (interfaces, IP, gateway)",
    .flags = NULL
};

