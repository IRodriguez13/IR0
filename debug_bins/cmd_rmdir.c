/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: rmdir
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Remove directory command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_rmdir_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: rmdir <dirname>\n");
        return 1;
    }
    
    const char *dirname = argv[1];
    
    /* Use POSIX rmdir syscall */
    int64_t result = syscall(SYS_RMDIR, (uint64_t)dirname, 0, 0);
    if (result < 0)
    {
        debug_write_err("rmdir: failed\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_rmdir = {
    .name = "rmdir",
    .handler = cmd_rmdir_handler,
    .usage = "rmdir DIR",
    .description = "Remove directory"
};



