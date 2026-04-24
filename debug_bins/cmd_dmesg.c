/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_dmesg.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: dmesg
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Show kernel messages command (uses only syscalls)
 */

#include "debug_bins.h"

static int cmd_dmesg_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    /* dmesg is cat /proc/kmsg (kernel log buffer, includes boot logs) */
    char *cat_argv[] = {"cat", "/proc/kmsg", NULL};
    extern struct debug_command cmd_cat;
    int ret = cmd_cat.handler(2, cat_argv);
    if (ret == 0)
        debug_serial_ok("dmesg");
    else
        debug_serial_fail("dmesg", "cat");
    return ret;
}

struct debug_command cmd_dmesg = {
    .name = "dmesg",
    .handler = cmd_dmesg_handler,
    .usage = "dmesg",
    .description = "Show kernel log buffer"
};



