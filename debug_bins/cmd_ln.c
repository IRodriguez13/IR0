/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: ln
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Create hard link command using POSIX syscalls only
 */

#include "debug_bins.h"

static int cmd_ln_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("Usage: ln <oldpath> <newpath>\n");
        debug_serial_fail("ln", "usage");
        return 1;
    }
    
    const char *oldpath = argv[1];
    const char *newpath = argv[2];
    
    /* Use POSIX link syscall */
    int64_t result = syscall(SYS_LINK, (uint64_t)oldpath, (uint64_t)newpath, 0);
    if (result < 0)
    {
        debug_write_err("ln: failed\n");
        debug_serial_fail("ln", "link");
        return 1;
    }
    
    debug_serial_ok("ln");
    return 0;
}

struct debug_command cmd_ln = {
    .name = "ln",
    .handler = cmd_ln_handler,
    .usage = "ln OLDPATH NEWPATH",
    .description = "Create hard link"
};

