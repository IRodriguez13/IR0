/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: mkdir
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Create directory command (uses only syscalls)
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_mkdir_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: mkdir <dirname>\n");
        return 1;
    }
    
    const char *dirname = argv[1];
    int64_t result = ir0_mkdir(dirname, 0755);
    if (result < 0)
    {
        debug_write_err("mkdir: failed\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_mkdir = {
    .name = "mkdir",
    .handler = cmd_mkdir_handler,
    .usage = "mkdir DIR",
    .description = "Create directory"
};

