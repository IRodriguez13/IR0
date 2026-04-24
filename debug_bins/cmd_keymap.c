/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_keymap.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: keymap
 *
 * Query or set keyboard layout through custom syscalls.
 * Usage:
 *   keymap          -> print current layout
 *   keymap us       -> set US layout
 *   keymap latam    -> set LATAM layout
 */

#include "debug_bins.h"

#define KEYMAP_LAYOUT_US     0
#define KEYMAP_LAYOUT_LATAM  1

static int keymap_print_current(void)
{
    int64_t layout = syscall(SYS_KEYMAP_GET, 0, 0, 0);
    char line[80];

    if (layout < 0)
    {
        debug_writeln_err("keymap: failed to get current layout");
        debug_serial_fail_err("keymap", "get", (int)(-layout));
        return 1;
    }

    if (layout == KEYMAP_LAYOUT_LATAM)
        snprintf(line, sizeof(line), "keymap: current layout = latam\n");
    else
        snprintf(line, sizeof(line), "keymap: current layout = us\n");

    debug_write(line);
    return 0;
}

static int cmd_keymap_handler(int argc, char **argv)
{
    if (argc == 1)
    {
        int rc = keymap_print_current();
        if (rc == 0)
            debug_serial_ok("keymap");
        return rc;
    }

    if (argc != 2)
    {
        debug_writeln_err("Usage: keymap [us|latam]");
        debug_serial_fail("keymap", "usage");
        return 1;
    }

    int layout = -1;
    if (strcmp(argv[1], "us") == 0)
        layout = KEYMAP_LAYOUT_US;
    else if (strcmp(argv[1], "latam") == 0)
        layout = KEYMAP_LAYOUT_LATAM;
    else
    {
        debug_writeln_err("keymap: invalid layout. Use us or latam");
        debug_serial_fail("keymap", "invalid_layout");
        return 1;
    }

    int64_t rc = syscall(SYS_KEYMAP_SET, layout, 0, 0);
    if (rc < 0)
    {
        debug_writeln_err("keymap: failed to set layout");
        debug_serial_fail_err("keymap", "set", (int)(-rc));
        return 1;
    }

    if (layout == KEYMAP_LAYOUT_LATAM)
        debug_writeln("keymap: switched to latam");
    else
        debug_writeln("keymap: switched to us");

    debug_serial_ok("keymap");
    return 0;
}

struct debug_command cmd_keymap = {
    .name = "keymap",
    .handler = cmd_keymap_handler,
    .usage = "keymap [us|latam]",
    .description = "Get or set keyboard layout"
};
