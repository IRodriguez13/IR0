/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: whoami
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Minimal whoami implementation using available identity syscalls.
 * Without passwd/group databases, non-root users are rendered as uidN.
 */

#include "debug_bins.h"

static int cmd_whoami_handler(int argc, char **argv)
{
    (void)argv;
    if (argc != 1)
    {
        debug_write_err("Usage: whoami\n");
        debug_serial_fail("whoami", "usage");
        return 1;
    }

    int64_t euid = syscall(SYS_GETEUID, 0, 0, 0);
    if (euid < 0)
    {
        debug_write_err("whoami: could not read effective uid\n");
        debug_serial_fail("whoami", "syscall");
        return 1;
    }

    if (euid == 0)
    {
        debug_write("root\n");
    }
    else
    {
        char uid_txt[32];
        debug_u64_to_dec((uint64_t)euid, uid_txt, sizeof(uid_txt));
        debug_write(uid_txt);
        debug_write("\n");
    }

    debug_serial_ok("whoami");
    return 0;
}

struct debug_command cmd_whoami = {
    .name = "whoami",
    .handler = cmd_whoami_handler,
    .usage = "whoami",
    .description = "Print effective user name"
};
