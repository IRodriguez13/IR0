/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_mkdir.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: mkdir
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Create directory command (uses only syscalls)
 */

#include "debug_bins.h"

static int cmd_mkdir_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("mkdir: expected argument\n");
        debug_serial_fail("mkdir", "usage");
        return -1;
    }

    const char *dirname = argv[1];
    int64_t result = ir0_mkdir(dirname, 0755);
    if (result < 0)
    {
        debug_perror("mkdir", dirname, (int)result);
        debug_serial_fail_err("mkdir", "vfs", (int)(-result));
        return -1;
    }

    debug_write("mkdir: created '");
    debug_write(dirname);
    debug_write("'\n");
    debug_serial_ok("mkdir");
    return 0;
}

struct debug_command cmd_mkdir = {
    .name = "mkdir",
    .handler = cmd_mkdir_handler,
    .usage = "mkdir DIR",
    .description = "Create directory"
};
