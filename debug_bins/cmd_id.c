/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: cmd_id.c
 * Description: Debug binary: id
 */

#include "debug_bins.h"

static int cmd_id_handler(int argc, char **argv)
{
    (void)argv;
    if (argc != 1)
    {
        debug_write_err("Usage: id\n");
        debug_serial_fail("id", "usage");
        return 1;
    }

    int64_t uid = syscall(SYS_GETUID, 0, 0, 0);
    int64_t gid = syscall(SYS_GETGID, 0, 0, 0);
    int64_t euid = syscall(SYS_GETEUID, 0, 0, 0);
    int64_t egid = syscall(SYS_GETEGID, 0, 0, 0);

    if (uid < 0 || gid < 0 || euid < 0 || egid < 0)
    {
        debug_write_err("id: could not read process credentials\n");
        debug_serial_fail("id", "syscall");
        return 1;
    }

    char line[192];
    snprintf(line, sizeof(line),
             "uid=%u gid=%u euid=%u egid=%u\n",
             (unsigned)uid, (unsigned)gid, (unsigned)euid, (unsigned)egid);
    debug_write(line);
    debug_serial_ok("id");
    return 0;
}

struct debug_command cmd_id = {
    .name = "id",
    .handler = cmd_id_handler,
    .usage = "id",
    .description = "Print user and group identifiers"
};
