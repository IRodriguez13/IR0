/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_rmdir.c
 * Description: IR0 kernel source/header file
 */

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

    int had_error = 0;
    for (int i = 1; i < argc; i++)
    {
        const char *dirname = argv[i];

        /* Safety: never allow rmdir / */
        if (dirname[0] == '/' && (dirname[1] == '\0' || (dirname[1] == '.' && dirname[2] == '\0')))
        {
            debug_write_err("rmdir: cannot remove root directory\n");
            debug_serial_fail("rmdir", "root");
            had_error = 1;
            continue;
        }

        int64_t result = ir0_rmdir(dirname);
        if (result < 0)
        {
            debug_perror("rmdir", dirname, (int)result);
            if (result == -ENOTEMPTY)
                debug_write_err("Hint: Directory must be empty. Remove contents first.\n");
            debug_serial_fail_err("rmdir", "vfs", (int)(-result));
            had_error = 1;
            continue;
        }

        {
            char line[544];
            int n = snprintf(line, sizeof(line), "rmdir: removed '%s'\n", dirname);

            if (n > 0 && n < (int)sizeof(line))
                debug_write(line);
        }
    }

    if (had_error)
        return 1;

    debug_serial_ok("rmdir");
    return 0;
}

struct debug_command cmd_rmdir = {
    .name = "rmdir",
    .handler = cmd_rmdir_handler,
    .usage = "rmdir DIR...",
    .description = "Remove directory"
};
