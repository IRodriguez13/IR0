/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_mount.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: mount
 * With no args: read /proc/mounts (raw) and format. With args: mount.
 */

#include "debug_bins.h"
#include <string.h>

#define MOUNT_BUF_SIZE 1024

static int cmd_mount_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        /* List mounts: read raw /proc/mounts (device\tmountpoint\tfstype\toptions) */
        int fd = syscall(SYS_OPEN, (uint64_t)"/proc/mounts", 0, 0);
        if (fd < 0)
        {
            debug_writeln_err("mount: cannot open /proc/mounts");
            debug_serial_fail("mount", "open_proc");
            return -1;
        }
        char buf[MOUNT_BUF_SIZE];
        int nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, sizeof(buf) - 1);
        syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
        if (nr <= 0) return 0;
        buf[nr] = '\0';
        debug_writeln("device     on  mountpoint  type   options");
        debug_writeln("----------------------------------------");
        const char *p = buf;
        while (*p)
        {
            const char *eol = strchr(p, '\n');
            if (!eol) break;
            char dev[64], path[64], type[32], opts[32];
            dev[0] = path[0] = type[0] = opts[0] = '\0';
            char *cur = (char *)p;
            char *dst = dev; size_t dlen = sizeof(dev);
            for (int f = 0; f < 4 && *cur; f++)
            {
                if (f == 1) { dst = path; dlen = sizeof(path); }
                else if (f == 2) { dst = type; dlen = sizeof(type); }
                else if (f == 3) { dst = opts; dlen = sizeof(opts); }
                size_t j = 0;
                while (*cur && *cur != '\t' && *cur != '\n' && j < dlen - 1)
                    dst[j++] = *cur++;
                dst[j] = '\0';
                if (*cur == '\t') cur++;
            }
            if (dev[0] || path[0])
            {
                char line[160];
                snprintf(line, sizeof(line), "%-10s on %-11s %-6s %s", dev, path, type, opts);
                debug_writeln(line);
            }
            p = eol + 1;
        }
        debug_serial_ok("mount");
        return 0;
    }
    if (argc < 3)
    {
        debug_write_err("Usage: mount [DEV MOUNTPOINT [fstype]]\n");
        debug_serial_fail("mount", "usage");
        return 1;
    }
    const char *dev = argv[1];
    const char *mountpoint = argv[2];
    const char *fstype = (argc >= 4) ? argv[3] : NULL;
    int64_t result = syscall(SYS_MOUNT, (uint64_t)dev, (uint64_t)mountpoint,
                             (uint64_t)(fstype ? fstype : ""));
    if (result < 0)
    {
        debug_write_err("mount: failed\n");
        debug_serial_fail("mount", "vfs");
        return 1;
    }
    debug_serial_ok("mount");
    return 0;
}

struct debug_command cmd_mount = {
    .name = "mount",
    .handler = cmd_mount_handler,
    .usage = "mount [DEV MOUNTPOINT [fstype]]",
    .description = "Mount filesystem"
};

