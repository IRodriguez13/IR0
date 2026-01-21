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
        debug_write_err("Usage: touch FILE\n");
        return 1;
    }
    
    const char *filename = argv[1];
    
    /* Touch: open file with O_CREAT (creates if doesn't exist) */
    int fd = syscall(SYS_OPEN, (uint64_t)filename, O_WRONLY | O_CREAT, 0644);
    if (fd < 0)
    {
        debug_write_err("touch: failed\n");
        return 1;
    }
    
    syscall(SYS_CLOSE, fd, 0, 0);
    return 0;
}

struct debug_command cmd_touch = {
    .name = "touch",
    .handler = cmd_touch_handler,
    .usage = "touch FILE",
    .description = "Create empty file or update timestamp"
};

