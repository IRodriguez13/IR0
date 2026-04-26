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

static const char *const mount_flags[] = {
    "-t",
    "-h",
    "--help",
    NULL
};

static int split_ws_fields(char *line, char **fields, int max_fields)
{
    int count = 0;
    char *p = line;

    while (*p && count < max_fields)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0' || *p == '\n')
            break;

        fields[count++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n')
            p++;
        if (*p == '\0' || *p == '\n')
            break;
        *p++ = '\0';
    }

    return count;
}

static void mount_print_key_device_state(const char *path, int open_flags)
{
    char line[160];
    int access_ok = (int)syscall(SYS_ACCESS, (uint64_t)path, 0, 0);
    int fd = -1;

    if (access_ok == 0)
    {
        fd = (int)syscall(SYS_OPEN, (uint64_t)path, (uint64_t)open_flags, 0);
    }

    snprintf(line, sizeof(line), "%-12s present=%s usable=%s",
             path,
             access_ok == 0 ? "yes" : "no",
             fd >= 0 ? "yes" : "no");
    debug_writeln(line);

    if (fd >= 0)
        syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
}

static void mount_print_usage(void)
{
    debug_writeln("usage:");
    debug_writeln("  mount");
    debug_writeln("  mount DEV MOUNTPOINT [fstype]");
    debug_writeln("  mount -t fstype DEV MOUNTPOINT");
    debug_writeln("");
    debug_writeln("examples:");
    debug_writeln("  mount /dev/null /mnt tmpfs");
    debug_writeln("  mount -t tmpfs /dev/null /mnt");
    debug_writeln("  umount /mnt");
    debug_writeln("  mount /dev/simple0 /mnt/simple simplefs");
    debug_writeln("  mount /dev/fat0 /mnt/fat fat16");
}

static int cmd_mount_handler(int argc, char **argv)
{
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        mount_print_usage();
        debug_serial_ok("mount");
        return 0;
    }

    if (argc < 2)
    {
        /* List mounts: read raw /proc/mounts (whitespace-separated fields). */
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
            if (eol > p)
            {
                char line_raw[256];
                char *fields[6] = {0};
                size_t len = (size_t)(eol - p);
                if (len >= sizeof(line_raw))
                    len = sizeof(line_raw) - 1;
                memcpy(line_raw, p, len);
                line_raw[len] = '\0';

                int nfields = split_ws_fields(line_raw, fields, 6);
                if (nfields >= 4)
                {
                    char line[160];
                    snprintf(line, sizeof(line), "%-10s on %-11s %-6s %s",
                             fields[0], fields[1], fields[2], fields[3]);
                    debug_writeln(line);
                }
            }
            p = eol + 1;
        }

        fd = syscall(SYS_OPEN, (uint64_t)"/proc/filesystems", 0, 0);
        if (fd >= 0)
        {
            nr = syscall(SYS_READ, (uint64_t)fd, (uint64_t)buf, sizeof(buf) - 1);
            syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
            if (nr > 0)
            {
                buf[nr] = '\0';
                debug_writeln("");
                debug_writeln("available fs types:");
                debug_writeln("-------------------");
                p = buf;
                while (*p)
                {
                    const char *eol = strchr(p, '\n');
                    if (!eol)
                        break;
                    if (eol > p)
                    {
                        char line_raw[64];
                        char *fields[3] = {0};
                        size_t len = (size_t)(eol - p);
                        if (len >= sizeof(line_raw))
                            len = sizeof(line_raw) - 1;
                        memcpy(line_raw, p, len);
                        line_raw[len] = '\0';

                        if (split_ws_fields(line_raw, fields, 3) >= 1)
                        {
                            const char *name = fields[0];
                            if (strcmp(name, "nodev") == 0 && fields[1])
                                name = fields[1];
                            debug_writeln(name);
                        }
                    }
                    p = eol + 1;
                }
            }
        }

        debug_writeln("");
        debug_writeln("key /dev usability:");
        debug_writeln("-------------------");
        mount_print_key_device_state("/dev/net", O_WRONLY);
        mount_print_key_device_state("/dev/disk", O_RDONLY);
        mount_print_key_device_state("/dev/audio", O_WRONLY);
        debug_serial_ok("mount");
        return 0;
    }

    const char *dev = NULL;
    const char *mountpoint = NULL;
    const char *fstype = NULL;

    if (argc >= 5 && strcmp(argv[1], "-t") == 0)
    {
        fstype = argv[2];
        dev = argv[3];
        mountpoint = argv[4];
    }
    else if (argc >= 3)
    {
        dev = argv[1];
        mountpoint = argv[2];
        fstype = (argc >= 4) ? argv[3] : NULL;
    }

    if (!dev || !mountpoint)
    {
        mount_print_usage();
        debug_serial_fail("mount", "usage");
        return 1;
    }

    int64_t result = syscall(SYS_MOUNT, (uint64_t)dev, (uint64_t)mountpoint,
                             (uint64_t)(fstype ? fstype : ""));
    if (result < 0)
    {
        debug_write_err("mount: failed");
        if (!fstype || fstype[0] == '\0')
            debug_write_err(" (hint: specify fstype, e.g. tmpfs)");
        debug_write_err("\n");
        debug_perror("mount", mountpoint, (int)result);
        debug_serial_fail("mount", "vfs");
        return 1;
    }
    debug_serial_ok("mount");
    return 0;
}

struct debug_command cmd_mount = {
    .name = "mount",
    .handler = cmd_mount_handler,
    .usage = "mount [DEV MOUNTPOINT [fstype] | -t fstype DEV MOUNTPOINT]",
    .description = "List or mount filesystems",
    .flags = mount_flags
};

