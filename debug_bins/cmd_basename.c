/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: basename
 * Copyright (C) 2026 Iván Rodriguez
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_basename_handler(int argc, char **argv)
{
    if (argc != 2)
    {
        debug_write_err("Usage: basename PATH\n");
        debug_serial_fail("basename", "usage");
        return 1;
    }

    const char *path = argv[1];
    size_t len = strlen(path);
    if (len == 0)
    {
        debug_write(".\n");
        debug_serial_ok("basename");
        return 0;
    }

    while (len > 1 && path[len - 1] == '/')
        len--;

    if (len == 1 && path[0] == '/')
    {
        debug_write("/\n");
        debug_serial_ok("basename");
        return 0;
    }

    size_t start = len;
    while (start > 0 && path[start - 1] != '/')
        start--;

    syscall(SYS_WRITE, STDOUT_FILENO, (uint64_t)(path + start), len - start);
    debug_write("\n");

    debug_serial_ok("basename");
    return 0;
}

struct debug_command cmd_basename = {
    .name = "basename",
    .handler = cmd_basename_handler,
    .usage = "basename PATH",
    .description = "Strip directory prefix from path"
};
