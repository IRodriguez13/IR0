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
    return cmd_cat.handler(2, cat_argv);
}

struct debug_command cmd_dmesg = {
    .name = "dmesg",
    .handler = cmd_dmesg_handler,
    .usage = "dmesg",
    .description = "Show kernel log buffer"
};



