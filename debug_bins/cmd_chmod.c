/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_chmod.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: chmod
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Change file mode command using POSIX syscalls only
 */

#include "debug_bins.h"

/*
 * Parse octal mode (e.g. "755", "0644"). No libc strtol: avoids host
 * stdlib.h pulling in types that conflict with ir0/types.h.
 */
static int parse_octal_mode(const char *mode_str)
{
    int v = 0;

    if (!mode_str || mode_str[0] == '\0')
        return -1;

    while (*mode_str)
    {
        char c = *mode_str++;

        if (c < '0' || c > '7')
            return -1;
        v = (v << 3) | (c - '0');
        if (v > 07777)
            return -1;
    }
    return v;
}

static int cmd_chmod_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("chmod: expected MODE and at least one PATH\n");
        debug_serial_fail("chmod", "usage");
        return -1;
    }

    const char *mode_str = argv[1];

    int mode = parse_octal_mode(mode_str);
    if (mode < 0)
    {
        debug_write_err("chmod: invalid mode (expected octal like 755 or 4755)\n");
        debug_serial_fail("chmod", "parse");
        return -1;
    }

    int had_error = 0;
    for (int i = 2; i < argc; i++)
    {
        const char *path = argv[i];
        int64_t result = syscall(SYS_CHMOD, (uint64_t)path, (uint64_t)mode, 0);
        if (result < 0)
        {
            debug_perror("chmod", path, (int)result);
            debug_serial_fail_err("chmod", "chmod", (int)(-result));
            had_error = 1;
            continue;
        }

        debug_write("chmod: mode changed for '");
        debug_write(path);
        debug_write("'\n");
    }

    if (had_error)
        return -1;

    debug_serial_ok("chmod");
    return 0;
}

struct debug_command cmd_chmod = {
    .name = "chmod",
    .handler = cmd_chmod_handler,
    .usage = "chmod MODE PATH...",
    .description = "Change file mode"
};
