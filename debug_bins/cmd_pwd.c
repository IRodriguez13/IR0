/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_pwd.c
 * Description: IR0 kernel source/header file
 */

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
        debug_serial_ok("pwd");
        return 0;
    }
    else
    {
        debug_perror("pwd", "", (int)result);
        debug_serial_fail_err("pwd", "getcwd", (int)(-result));
        return 1;
    }
}

struct debug_command cmd_pwd = {
    .name = "pwd",
    .handler = cmd_pwd_handler,
    .usage = "pwd",
    .description = "Print working directory"
};

