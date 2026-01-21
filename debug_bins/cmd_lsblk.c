/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: lsblk
 * Copyright (C) 2025 Iván Rodriguez
 *
 * List block devices command (uses only syscalls)
 */

#include "debug_bins.h"

static int cmd_lsblk_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    /* Use cat to read from /proc/blockdevices */
    /* We'll call cmd_cat internally */
    char *cat_argv[] = {"cat", "/proc/blockdevices", NULL};
    extern struct debug_command cmd_cat;
    return cmd_cat.handler(2, cat_argv);
}

struct debug_command cmd_lsblk = {
    .name = "lsblk",
    .handler = cmd_lsblk_handler,
    .usage = "lsblk",
    .description = "List block devices"
};

