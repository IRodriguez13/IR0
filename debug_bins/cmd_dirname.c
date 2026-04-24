/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: dirname
 * Copyright (C) 2026 Iván Rodriguez
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_dirname_handler(int argc, char **argv)
{
    if (argc != 2)
    {
        debug_write_err("Usage: dirname PATH\n");
        debug_serial_fail("dirname", "usage");
        return 1;
    }

    char buffer[512];
    if (strlen(argv[1]) >= sizeof(buffer))
    {
        debug_write_err("dirname: path too long\n");
        debug_serial_fail_err("dirname", "path", ENAMETOOLONG);
        return 1;
    }

    strncpy(buffer, argv[1], sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    size_t len = strlen(buffer);
    while (len > 1 && buffer[len - 1] == '/')
    {
        buffer[len - 1] = '\0';
        len--;
    }

    char *last = strrchr(buffer, '/');
    if (!last)
    {
        debug_write(".\n");
    }
    else if (last == buffer)
    {
        debug_write("/\n");
    }
    else
    {
        *last = '\0';
        debug_write(buffer);
        debug_write("\n");
    }

    debug_serial_ok("dirname");
    return 0;
}

struct debug_command cmd_dirname = {
    .name = "dirname",
    .handler = cmd_dirname_handler,
    .usage = "dirname PATH",
    .description = "Strip non-directory suffix from path"
};
