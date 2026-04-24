/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_bluestart.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: bluestart
 * Inicia el escaneo Bluetooth (equivalente a: echo start > /proc/bluetooth/scan).
 * Solo syscalls: open/write/close a /proc/bluetooth/scan.
 *
 * El kernel trata sesiones Bluetooth como discriminables (análogo a conexiones WiFi);
 * enfoque experimental no habitual en otros Unix. Networking en este contexto es
 * avanzado para un hobby OS.
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>

#define SCAN_PATH   "/proc/bluetooth/scan"
#define CMD_START   "start\n"

static int cmd_bluestart_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int fd = (int)ir0_open(SCAN_PATH, O_WRONLY, 0);
    if (fd < 0)
    {
        debug_writeln_err("bluestart: cannot open " SCAN_PATH);
        debug_serial_fail("bluestart", "open");
        return 1;
    }

    size_t len = strlen(CMD_START);
    int64_t n = ir0_write(fd, CMD_START, len);
    ir0_close(fd);

    if (n != (int64_t)len)
    {
        debug_writeln_err("bluestart: failed to write start");
        debug_serial_fail("bluestart", "write");
        return 1;
    }

    debug_writeln("Bluetooth scan started.");
    debug_serial_ok("bluestart");
    return 0;
}

struct debug_command cmd_bluestart = {
    .name = "bluestart",
    .handler = cmd_bluestart_handler,
    .usage = "bluestart",
    .description = "Start Bluetooth scan (echo start > /proc/bluetooth/scan)"
};
