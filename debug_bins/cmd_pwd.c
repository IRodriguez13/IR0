/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: pwd
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Print working directory command (uses only syscalls)
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_pwd_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    char cwd[256];
    int64_t result = syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0);
    if (result >= 0)
    {
        debug_write(cwd);
        debug_write("\n");
        return 0;
    }
    else
    {
        debug_write_err("pwd: failed\n");
        return 1;
    }
}

struct debug_command cmd_pwd = {
    .name = "pwd",
    .handler = cmd_pwd_handler,
    .usage = "pwd",
    .description = "Print working directory"
};

