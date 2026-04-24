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
 * Show disk space command (uses only syscalls).
 * Reads /dev/disk and copies to stdout; errors use debug_perror and serial.
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

static int cmd_df_handler(int argc, char **argv)
{
    const char *disk_path = "/dev/disk";
    int fd;
    int64_t n;

    (void)argc;
    (void)argv;

    fd = ir0_open(disk_path, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_perror("df", disk_path, (int)fd);
        debug_serial_fail_err("df", "open", (int)(-fd));
        return 1;
    }

    for (;;)
    {
        char buf[512];

        n = ir0_read(fd, buf, sizeof(buf));
        if (n < 0)
        {
            ir0_close(fd);
            debug_perror("df", disk_path, (int)n);
            debug_serial_fail_err("df", "read", (int)(-n));
            return 1;
        }
        if (n == 0)
            break;
        syscall(SYS_WRITE, 1, (uint64_t)buf, (uint64_t)n);
    }

    ir0_close(fd);
    debug_serial_ok("df");
    return 0;
}

struct debug_command cmd_df = {
    .name = "df",
    .handler = cmd_df_handler,
    .usage = "df",
    .description = "Show disk space"
};
