/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: rmdir
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Remove directory (OSDev-style: perror, syscalls only).
 * POSIX: only removes empty directories (ENOTEMPTY if non-empty).
 * Blocks rmdir / for safety.
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_rmdir_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("rmdir: expected argument\n");
        debug_serial_fail("rmdir", "usage");
        return 1;
    }

    const char *dirname = argv[1];

    /* Safety: never allow rmdir / */
    if (dirname[0] == '/' && (dirname[1] == '\0' || (dirname[1] == '.' && dirname[2] == '\0')))
    {
        debug_write_err("rmdir: cannot remove root directory\n");
        debug_serial_fail("rmdir", "root");
        return 1;
    }

    int64_t result = ir0_rmdir(dirname);
    if (result < 0)
    {
        debug_perror("rmdir", dirname, (int)result);
        if (result == -ENOTEMPTY)
            debug_write_err("Hint: Directory must be empty. Remove contents first.\n");
        debug_serial_fail_err("rmdir", "vfs", (int)(-result));
        return 1;
    }

    debug_serial_ok("rmdir");
    return 0;
}

struct debug_command cmd_rmdir = {
    .name = "rmdir",
    .handler = cmd_rmdir_handler,
    .usage = "rmdir DIR",
    .description = "Remove directory"
};
