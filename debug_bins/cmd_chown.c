/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: chown
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Change file owner command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/syscall.h>
#include <string.h>
#include <stdlib.h>

static int cmd_chown_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("Usage: chown USER[:GROUP] PATH\n");
        debug_serial_fail("chown", "usage");
        return 1;
    }
    
    char owner_buf[64];

    strncpy(owner_buf, argv[1], sizeof(owner_buf) - 1);
    
    owner_buf[sizeof(owner_buf) - 1] = '\0';
    
    const char *path = argv[2];
    
    uid_t owner = (uid_t)-1;
    gid_t group = (gid_t)-1;
    
    char *colon = strchr(owner_buf, ':');
    if (colon)
    {
        *colon = '\0';
        owner = (uid_t)atoi(owner_buf);
        group = (gid_t)atoi(colon + 1);
    }
    else
    {
        owner = (uid_t)atoi(owner_buf);
    }
    
    int64_t ret = syscall(SYS_CHOWN, (uint64_t)path, owner, group);
    if (ret < 0)
    {
        debug_perror("chown", path, (int)ret);
        debug_serial_fail_err("chown", "syscall", (int)(-ret));
        return 1;
    }
    debug_serial_ok("chown");
    return 0;
}

struct debug_command cmd_chown = 
{
    .name = "chown",
    .handler = cmd_chown_handler,
    .usage = "chown USER[:GROUP] PATH",
    .description = "Change file owner and group"
};





