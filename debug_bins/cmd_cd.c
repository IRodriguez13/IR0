/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: cd
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Change directory command (uses only syscalls)
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_cd_handler(int argc, char **argv)
{
    const char *dirname = "/";
    
    if (argc > 1)
    {
        dirname = argv[1];
    }
    
    int64_t result = syscall(SYS_CHDIR, (uint64_t)dirname, 0, 0);
    if (result < 0)
    {
        debug_perror("cd", dirname, (int)result);
        debug_serial_fail_err("cd", "chdir", (int)(-result));
        return 1;
    }
    
    debug_serial_ok("cd");
    return 0;
}

struct debug_command cmd_cd = {
    .name = "cd",
    .handler = cmd_cd_handler,
    .usage = "cd [DIR]",
    .description = "Change directory"
};

