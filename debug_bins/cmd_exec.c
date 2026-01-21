/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: exec
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Execute binary command using POSIX syscalls only
 */

#include "debug_bins.h"

static int cmd_exec_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: exec <filename>\n");
        return 1;
    }
    
    const char *filename = argv[1];
    
    /* Use POSIX exec syscall */
    int64_t result = syscall(SYS_EXEC, (uint64_t)filename, 0, 0);
    if (result < 0)
    {
        debug_write_err("exec: failed\n");
        return 1;
    }
    
    /* Should not reach here if exec succeeds */
    return 0;
}

struct debug_command cmd_exec = {
    .name = "exec",
    .handler = cmd_exec_handler,
    .usage = "exec FILE",
    .description = "Execute binary"
};



