/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System Core Utils.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_chmod.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: chmod
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Change file mode command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/stat.h>

/*
 * Parse octal mode (e.g. "755", "0644"). No libc strtol: avoids host
 * stdlib.h pulling in types that conflict with ir0/types.h.
 */
static inline int parse_octal_mode(const char *mode_str)
{
    int v = 0;

    if (!mode_str || mode_str[0] == '\0')
        return -1;

    while (*mode_str)
    {
        char c = *mode_str++;

        if (c < '0' || c > '7')
            return -1;
       
        v = (v << 3) | (c - '0');
       
            if (v > 07777)
            return -1;
    }
    return v;
}

/*
 * Print usage information
 */
static void print_usage(void)
{
    debug_write("Usage: chmod MODE PATH...\n");
    debug_write("Change file mode.\n");
    debug_write("MODE can be octal (e.g., 755) or symbolic (e.g., u+rwx,g-w)\n");
}

/*
 * Apply symbolic mode changes to current mode
 */
static inline int apply_symbolic_mode(const char *mode_str, int current_mode)
{
    int who = 0; // 1=user, 2=group, 4=other
    char op = 0;
    int perm = 0;

    const char *p = mode_str;

    // Parse who (u, g, o, a)
    while (*p && *p != '+' && *p != '-' && *p != '=')
    {
        if (*p == 'u') who |= 1;
        else if (*p == 'g') who |= 2;
        else if (*p == 'o') who |= 4;
        else if (*p == 'a') who = 7;
        p++;
    }

    if (!who) who = 7; // default to all

    op = *p++;
    if (!op) return -1; // invalid

    // Parse permissions (r, w, x)
    while (*p)
    {
        if (*p == 'r') perm |= 4;
        else if (*p == 'w') perm |= 2;
        else if (*p == 'x') perm |= 1;
        p++;
    }

    int new_mode = current_mode & ~0777; // keep file type bits

    if (op == '+')
    {
        new_mode |= current_mode & 0777;
        if (who & 1) new_mode |= (perm << 6);
        if (who & 2) new_mode |= (perm << 3);
        if (who & 4) new_mode |= perm;
    }
    else if (op == '-')
    {
        new_mode |= current_mode & 0777;
        if (who & 1) new_mode &= ~(perm << 6);
        if (who & 2) new_mode &= ~(perm << 3);
        if (who & 4) new_mode &= ~perm;
    }
    else if (op == '=')
    {
        new_mode |= current_mode & 0777;
        // Clear existing perms for who
        if (who & 1) new_mode &= ~0700;
        if (who & 2) new_mode &= ~0070;
        if (who & 4) new_mode &= ~0007;
        // Set new perms
        if (who & 1) new_mode |= (perm << 6);
        if (who & 2) new_mode |= (perm << 3);
        if (who & 4) new_mode |= perm;
    }
    else
    {
        return -1; // invalid op
    }

    return new_mode;
}

static int cmd_chmod_handler(int argc, char **argv)
{
    // Check for --help or -h
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))
    {
        print_usage();
        return 0;
    }
    
    if (argc < 3)
    {
        debug_write_err("chmod: expected MODE and at least one PATH\n");
        debug_serial_fail("chmod", "usage");
        return -1;
    }


    const char *mode_str = argv[1];

    int mode = parse_octal_mode(mode_str);
    int is_octal = (mode >= 0);

    int had_error = 0;
    for (int i = 2; i < argc; i++)
    {
        const char *path = argv[i];
        int final_mode;

        if (is_octal)
        {
            final_mode = mode;
        }
        else
        {
            // Get current mode for symbolic changes
            struct stat st;
            int64_t result = syscall2(SYS_STAT, (uint64_t)path, (uint64_t)&st);
            if (result < 0)
            {
                debug_perror("chmod", path, (int)result);
                debug_serial_fail_err("chmod", "stat", (int)(-result));
                had_error = 1;
                continue;
            }
            final_mode = apply_symbolic_mode(mode_str, st.st_mode);
            if (final_mode < 0)
            {
                debug_write_err("chmod: invalid symbolic mode\n");
                debug_serial_fail("chmod", "parse");
                had_error = 1;
                continue;
            }
        }

        int64_t result = syscall2(SYS_CHMOD, (uint64_t)path, (uint64_t)final_mode);
        if (result < 0)
        {
            debug_perror("chmod", path, (int)result);
            debug_serial_fail_err("chmod", "chmod", (int)(-result));
            had_error = 1;
            continue;
        }

        debug_write("chmod: mode changed for '");
        debug_write(path);
        debug_write("'\n");
    }

    if (had_error)
        return -1;

    debug_serial_ok("chmod");
    return 0;
}

struct debug_command cmd_chmod = {
    .name = "chmod",
    .handler = cmd_chmod_handler,
    .usage = "chmod MODE PATH...",
    .description = "Change file mode (MODE: octal or symbolic)"
};
