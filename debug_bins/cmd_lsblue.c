/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_lsblue.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: lsblue
 * Copyright (C) 2025  Iván Rodriguez
 *
 * List Bluetooth topology (adapters and visible neighbors).
 * Analogous to ip/ifconfig for Bluetooth: exposes each device as observable node.
 * Reads from /sys/class/bluetooth/ (hci0/address, hci0/state, topology/neighbors).
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>
#include <stdint.h>

#define NEIGH_BUF_SIZE 2048
#define LINE_MAX       256

/*
 * Parse one line "ADDRESS RSSI NAME" (NAME may contain spaces) and print formatted row.
 * Returns 0 on success, -1 on parse error or empty line.
 */
static int print_neighbor_line(const char *line, const char *adapter)
{
    char addr[32];
    int rssi = 0;
    char name[160];
    const char *p;
    size_t i;

    if (!line || !line[0] || line[0] == '\n')
        return -1;

    /* First token: address */
    p = line;
    while (*p == ' ') p++;
    i = 0;
    while (i < sizeof(addr) - 1 && *p && *p != ' ' && *p != '\n')
        addr[i++] = *p++;
    addr[i] = '\0';
    if (i == 0)
        return -1;

    /* Second token: rssi (number) */
    while (*p == ' ') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9'))
    {
        int neg = 0;
        if (*p == '-') { neg = 1; p++; }
        rssi = 0;
        while (*p >= '0' && *p <= '9')
        {
            rssi = rssi * 10 + (int)(*p - '0');
            p++;
        }
        if (neg)
            rssi = -rssi;
    }

    /* Rest of line: name */
    while (*p == ' ') p++;
    i = 0;
    while (i < sizeof(name) - 1 && *p && *p != '\n')
        name[i++] = *p++;
    name[i] = '\0';

    {
        char row[280];
        int len = snprintf(row, sizeof(row), "%-6s %-18s %-20s %-10s %d\n",
                           adapter, addr, name[0] ? name : "(unknown)", "VISIBLE", rssi);
        if (len > 0 && len < (int)sizeof(row))
        {
            debug_write(row);
            return 0;
        }
    }
    return -1;
}

static int cmd_lsblue_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    int fd = ir0_open("/sys/class/bluetooth/topology/neighbors", O_RDONLY, 0);
    if (fd < 0)
    {
        debug_write_err("lsblue: cannot open /sys/class/bluetooth/topology/neighbors\n");
        debug_write_err("(Bluetooth subsystem may not be initialized or no scan done)\n");
        debug_serial_fail("lsblue", "open");
        return 1;
    }

    char buf[NEIGH_BUF_SIZE];
    int64_t nr = ir0_read(fd, buf, sizeof(buf) - 1);
    ir0_close(fd);

    if (nr <= 0)
    {
        debug_write("ADAPTER  ADDRESS             NAME                 STATE       RSSI\n");
        debug_write("(no neighbors; run: bluestart, then wait and lsblue or cat /proc/bluetooth/devices)\n");
        debug_serial_ok("lsblue");
        return 0;
    }

    buf[nr] = '\0';

    debug_write("ADAPTER  ADDRESS             NAME                 STATE       RSSI\n");

    const char *adapter = "hci0";
    const char *p = buf;
    char line[LINE_MAX];
    size_t li = 0;

    while (*p && li < sizeof(line) - 1)
    {
        if (*p == '\n')
        {
            line[li] = '\0';
            li = 0;
            print_neighbor_line(line, adapter);
            p++;
            continue;
        }
        line[li++] = *p++;
    }
    if (li > 0)
    {
        line[li] = '\0';
        print_neighbor_line(line, adapter);
    }

    debug_serial_ok("lsblue");
    return 0;
}

struct debug_command cmd_lsblue = {
    .name = "lsblue",
    .handler = cmd_lsblue_handler,
    .usage = "lsblue",
    .description = "List Bluetooth topology (adapters and visible devices)"
};
