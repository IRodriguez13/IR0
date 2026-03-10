/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: df
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Show disk space command (uses only syscalls)
 */

#include "debug_bins.h"

static int cmd_df_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    /* df is just cat /dev/disk */
    char *cat_argv[] = {"cat", "/dev/disk", NULL};
    extern struct debug_command cmd_cat;
    int ret = cmd_cat.handler(2, cat_argv);
    if (ret == 0)
        debug_serial_ok("df");
    else
        debug_serial_fail("df", "cat");
    return ret;
}

struct debug_command cmd_df = {
    .name = "df",
    .handler = cmd_df_handler,
    .usage = "df",
    .description = "Show disk space"
};



