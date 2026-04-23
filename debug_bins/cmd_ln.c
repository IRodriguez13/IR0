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
        debug_write_err("ln: expected target and link name\n");
        debug_serial_fail("ln", "usage");
        return -1;
    }

    const char *target = argv[1];
    const char *linkname = argv[2];

    int64_t result = syscall(SYS_LINK, (uint64_t)target, (uint64_t)linkname, 0);
    if (result < 0)
    {
        debug_perror("ln", linkname, (int)result);
        debug_serial_fail_err("ln", "link", (int)(-result));
        return -1;
    }

    debug_write("ln: '");
    debug_write(target);
    debug_write("' -> '");
    debug_write(linkname);
    debug_write("'\n");
    debug_serial_ok("ln");
    return 0;
}

struct debug_command cmd_ln = {
    .name = "ln",
    .handler = cmd_ln_handler,
    .usage = "ln OLDPATH NEWPATH",
    .description = "Create hard link"
};
