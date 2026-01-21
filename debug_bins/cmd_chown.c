/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: chown
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Change file owner command using POSIX syscalls only
 * Note: Currently not fully implemented in kernel
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_chown_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("Usage: chown USER PATH\n");
        debug_write_err("Note: chown is not yet fully implemented\n");
        return 1;
    }
    
    (void)argv;  /* Not implemented yet */
    debug_write_err("chown: not yet implemented\n");
    return 1;
}

struct debug_command cmd_chown = {
    .name = "chown",
    .handler = cmd_chown_handler,
    .usage = "chown USER PATH",
    .description = "Change file owner (not implemented)"
};



