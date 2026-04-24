/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_sudo.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: sudo
 *
 * Minimal sudo MVP:
 * - validates credentials through SYS_SUDO_AUTH
 * - runs one debug command with elevated euid
 * - drops credentials back to real uid/gid
 */

#include "debug_bins.h"
#include <string.h>

static const char *const sudo_flags[] = {
    "-p",
    NULL
};

static int cmd_sudo_handler(int argc, char **argv)
{
    int cmd_index = 1;
    const char *password = "ir0";
    int64_t real_uid;
    int64_t real_gid;
    int64_t auth_rc;

    if (argc < 2)
    {
        debug_writeln_err("Usage: sudo [-p password] <command> [args...]");
        debug_serial_fail("sudo", "usage");
        return 1;
    }

    if (strcmp(argv[1], "-p") == 0)
    {
        if (argc < 4)
        {
            debug_writeln_err("sudo: missing password or command");
            debug_serial_fail("sudo", "usage");
            return 1;
        }
        password = argv[2];
        cmd_index = 3;
    }

    if (cmd_index >= argc)
    {
        debug_writeln_err("sudo: missing command");
        debug_serial_fail("sudo", "usage");
        return 1;
    }

    real_uid = syscall(SYS_GETUID, 0, 0, 0);
    real_gid = syscall(SYS_GETGID, 0, 0, 0);
    if (real_uid < 0 || real_gid < 0)
    {
        debug_writeln_err("sudo: failed to read current identity");
        debug_serial_fail("sudo", "getid");
        return 1;
    }

    auth_rc = syscall(SYS_SUDO_AUTH, (uint64_t)password, 0, 0);
    if (auth_rc < 0)
    {
        debug_writeln_err("sudo: authentication failed");
        debug_serial_fail_err("sudo", "auth", (int)(-auth_rc));
        return 1;
    }

    struct debug_command *sub = debug_find_command(argv[cmd_index]);
    if (!sub)
    {
        debug_writeln_err("sudo: command not found or invalid");
        (void)syscall(SYS_SETGID, (uint64_t)real_gid, 0, 0);
        (void)syscall(SYS_SETUID, (uint64_t)real_uid, 0, 0);
        debug_serial_fail("sudo", "lookup");
        return 1;
    }

    debug_serial_raw("[AUDIT] sudo: elevated command execution\n");
    int rc = sub->handler(argc - cmd_index, &argv[cmd_index]);

    if (syscall(SYS_SETGID, (uint64_t)real_gid, 0, 0) < 0 ||
        syscall(SYS_SETUID, (uint64_t)real_uid, 0, 0) < 0)
    {
        debug_writeln_err("sudo: failed to drop elevated credentials");
        debug_serial_fail("sudo", "drop");
        return 1;
    }

    if (rc == 0)
        debug_serial_ok("sudo");
    else
        debug_serial_fail("sudo", "command");
    return rc;
}

struct debug_command cmd_sudo = {
    .name = "sudo",
    .handler = cmd_sudo_handler,
    .usage = "sudo [-p password] <command> [args...]",
    .description = "Run command with elevated privileges (MVP)",
    .flags = sudo_flags
};
