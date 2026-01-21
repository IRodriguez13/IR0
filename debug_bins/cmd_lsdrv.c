/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: lsdrv
 * Copyright (C) 2025 Iván Rodriguez
 *
 * List drivers command (uses only syscalls)
 */

#include "debug_bins.h"

static int cmd_lsdrv_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    /* Use cat to read from /proc/drivers */
    char *cat_argv[] = {"cat", "/proc/drivers", NULL};
    extern struct debug_command cmd_cat;
    return cmd_cat.handler(2, cat_argv);
}

struct debug_command cmd_lsdrv = {
    .name = "lsdrv",
    .handler = cmd_lsdrv_handler,
    .usage = "lsdrv",
    .description = "List all registered drivers"
};

