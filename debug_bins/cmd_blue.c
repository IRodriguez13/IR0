/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_blue.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: blue
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Bluetooth topology control (analogous to ip). Uses only syscalls:
 * open/read/close on /sys/class/bluetooth/ (no kernel API calls).
 *
 * Usage: blue adapter | session | neigh
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>

#define BUF_SIZE 1024

static int read_and_print(const char *path)
{
    int fd = ir0_open(path, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_write_err("blue: cannot open ");
        debug_write_err(path);
        debug_write_err("\n");
        return 1;  /* Caller logs via cmd_blue_handler */
    }
    char buf[BUF_SIZE];
    int64_t nr = ir0_read(fd, buf, sizeof(buf) - 1);
    ir0_close(fd);
    if (nr > 0)
    {
        buf[nr] = '\0';
        debug_write(buf);
    }
    return 0;
}

static int cmd_blue_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: blue adapter | session | neigh\n");
        debug_serial_fail("blue", "usage");
        return 1;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "adapter") == 0)
    {
        debug_write("1: hci0:\n   address: ");
        if (read_and_print("/sys/class/bluetooth/hci0/address") != 0)
        {
            debug_serial_fail("blue", "open");
            return 1;
        }
        debug_write("   state: ");
        if (read_and_print("/sys/class/bluetooth/hci0/state") != 0)
        {
            debug_serial_fail("blue", "open");
            return 1;
        }
        debug_serial_ok("blue");
        return 0;
    }
    if (strcmp(sub, "session") == 0)
    {
        int r = read_and_print("/sys/class/bluetooth/sessions");
        if (r == 0) debug_serial_ok("blue");
        else debug_serial_fail("blue", "open");
        return r;
    }
    if (strcmp(sub, "neigh") == 0)
    {
        int r = read_and_print("/sys/class/bluetooth/topology/neighbors");
        if (r == 0) debug_serial_ok("blue");
        else debug_serial_fail("blue", "open");
        return r;
    }

    debug_write_err("blue: unknown subcommand '");
    debug_write_err(sub);
    debug_write_err("'\n");
    debug_serial_fail("blue", "subcmd");
    return 1;
}

struct debug_command cmd_blue = {
    .name = "blue",
    .handler = cmd_blue_handler,
    .usage = "blue adapter | session | neigh",
    .description = "Bluetooth topology (adapter/sessions/neighbors via sysfs)"
};
