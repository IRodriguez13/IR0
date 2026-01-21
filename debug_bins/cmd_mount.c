/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: mount
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Mount filesystem command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_mount_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("Usage: mount DEV MOUNTPOINT [fstype]\n");
        return 1;
    }
    
    const char *dev = argv[1];
    const char *mountpoint = argv[2];
    const char *fstype = (argc >= 4) ? argv[3] : NULL;
    
    /* Use POSIX mount syscall */
    int64_t result = syscall(SYS_MOUNT, (uint64_t)dev, (uint64_t)mountpoint, 
                            (uint64_t)(fstype ? fstype : ""));
    if (result < 0)
    {
        debug_write_err("mount: failed\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_mount = {
    .name = "mount",
    .handler = cmd_mount_handler,
    .usage = "mount DEV MOUNTPOINT [fstype]",
    .description = "Mount filesystem"
};

