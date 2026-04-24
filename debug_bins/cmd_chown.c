/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_chown.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: chown
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Change file owner command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <string.h>

/*
 * Parse a non-empty decimal string into uid/gid. Digits only; no host
 * stdlib atoi (stdlib.h conflicts with ir0 headers in this build).
 */
static int parse_id(const char *s, uint32_t *out)
{
    uint32_t v = 0;

    if (!s || s[0] == '\0')
        return -1;
    while (*s)
    {
        if (*s < '0' || *s > '9')
            return -1;
        uint32_t digit = (uint32_t)(*s - '0');
        if (v > (UINT32_MAX - digit) / 10u)
            return -1;
        v = v * 10u + digit;
        s++;
    }
    *out = v;
    return 0;
}

static int cmd_chown_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("chown: expected OWNER[:GROUP] and at least one PATH\n");
        debug_serial_fail("chown", "usage");
        return -1;
    }

    char owner_buf[64];

    strncpy(owner_buf, argv[1], sizeof(owner_buf) - 1);
    owner_buf[sizeof(owner_buf) - 1] = '\0';

    uid_t owner = (uid_t)-1;
    gid_t group = (gid_t)-1;

    char *colon = strchr(owner_buf, ':');
    if (colon)
    {
        uint32_t o = 0;
        uint32_t g = 0;

        *colon = '\0';
        if (owner_buf[0] == '\0' && (colon + 1)[0] == '\0')
        {
            debug_write_err("chown: empty owner and group\n");
            debug_serial_fail("chown", "parse");
            return -1;
        }
        if (owner_buf[0] != '\0')
        {
            if (parse_id(owner_buf, &o) != 0)
            {
                debug_write_err("chown: invalid owner id\n");
                debug_serial_fail("chown", "parse");
                return -1;
            }
            owner = (uid_t)o;
        }
        if ((colon + 1)[0] != '\0')
        {
            if (parse_id(colon + 1, &g) != 0)
            {
                debug_write_err("chown: invalid group id\n");
                debug_serial_fail("chown", "parse");
                return -1;
            }
            group = (gid_t)g;
        }
    }
    else
    {
        uint32_t o;

        if (parse_id(owner_buf, &o) != 0)
        {
            debug_write_err("chown: invalid user id\n");
            debug_serial_fail("chown", "parse");
            return -1;
        }
        owner = (uid_t)o;
    }

    int had_error = 0;
    for (int i = 2; i < argc; i++)
    {
        const char *path = argv[i];
        int64_t ret = syscall(SYS_CHOWN, (uint64_t)path, owner, group);
        if (ret < 0)
        {
            debug_perror("chown", path, (int)ret);
            debug_serial_fail_err("chown", "chown", (int)(-ret));
            had_error = 1;
            continue;
        }

        debug_write("chown: owner/group changed for '");
        debug_write(path);
        debug_write("'\n");
    }

    if (had_error)
        return -1;

    debug_serial_ok("chown");
    return 0;
}

struct debug_command cmd_chown = {
    .name = "chown",
    .handler = cmd_chown_handler,
    .usage = "chown OWNER[:GROUP] PATH...",
    .description = "Change file owner and group"
};
