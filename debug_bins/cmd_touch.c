/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: touch
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Touch command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

static int cmd_touch_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("touch: expected file argument\n");
        debug_serial_fail("touch", "usage");
        return -1;
    }

    const char *filename = argv[1];

    /*
     * If the file already exists, open(O_WRONLY) succeeds and we only
     * report an update after a successful close.
     */
    int64_t fd = syscall(SYS_OPEN, (uint64_t)filename, O_WRONLY, 0);

    if (fd >= 0)
    {
        int64_t cr = ir0_close((int)fd);

        if (cr < 0)
        {
            debug_perror("touch", filename, (int)cr);
            debug_serial_fail_err("touch", "close", (int)(-cr));
            return -1;
        }
        debug_write("touch: updated '");
        debug_write(filename);
        debug_write("'\n");
        debug_serial_ok("touch");
        return 0;
    }

    {
        int open_err = (int)fd;
        int e = (open_err < 0) ? -open_err : open_err;

        if (e != ENOENT)
        {
            debug_perror("touch", filename, open_err);
            debug_serial_fail_err("touch", "open", e);
            return -1;
        }
    }

    fd = syscall(SYS_OPEN, (uint64_t)filename, O_WRONLY | O_CREAT, 0644);
    if (fd < 0)
    {
        debug_perror("touch", filename, (int)fd);
        debug_serial_fail_err("touch", "open", (int)(-(int)fd));
        return -1;
    }

    {
        int64_t cr = ir0_close((int)fd);

        if (cr < 0)
        {
            debug_perror("touch", filename, (int)cr);
            debug_serial_fail_err("touch", "close", (int)(-cr));
            return -1;
        }
    }

    debug_write("touch: created '");
    debug_write(filename);
    debug_write("'\n");
    debug_serial_ok("touch");
    return 0;
}

struct debug_command cmd_touch = {
    .name = "touch",
    .handler = cmd_touch_handler,
    .usage = "touch FILE",
    .description = "Create empty file or update timestamp"
};
