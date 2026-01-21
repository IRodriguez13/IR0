/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ps
 * Copyright (C) 2026 Iván Rodriguez
 *
 * List processes command (uses only syscalls)
 */

#include "debug_bins.h"

static int cmd_ps_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    /* ps is just cat /proc/ps */
    char *cat_argv[] = {"cat", "/proc/ps", NULL};
    extern struct debug_command cmd_cat;
    return cmd_cat.handler(2, cat_argv);
}

struct debug_command cmd_ps = {
    .name = "ps",
    .handler = cmd_ps_handler,
    .usage = "ps",
    .description = "List processes"
};

