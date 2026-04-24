/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: stat
 * Copyright (C) 2026 Iván Rodriguez
 */

#include "debug_bins.h"
#include <ir0/stat.h>

static int cmd_stat_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: stat PATH...\n");
        debug_serial_fail("stat", "usage");
        return 1;
    }

    int had_error = 0;
    for (int i = 1; i < argc; i++)
    {
        const char *path = argv[i];
        stat_t st;
        int64_t ret = syscall(SYS_STAT, (uint64_t)path, (uint64_t)&st, 0);
        if (ret < 0)
        {
            debug_perror("stat", path, (int)ret);
            debug_serial_fail_err("stat", "stat", (int)(-ret));
            had_error = 1;
            continue;
        }

        char size_txt[32];
        char ino_txt[32];
        char nlink_txt[32];
        char uid_txt[32];
        char gid_txt[32];
        char line[256];

        debug_u64_to_dec((uint64_t)st.st_size, size_txt, sizeof(size_txt));
        debug_u64_to_dec((uint64_t)st.st_ino, ino_txt, sizeof(ino_txt));
        debug_u64_to_dec((uint64_t)st.st_nlink, nlink_txt, sizeof(nlink_txt));
        debug_u64_to_dec((uint64_t)st.st_uid, uid_txt, sizeof(uid_txt));
        debug_u64_to_dec((uint64_t)st.st_gid, gid_txt, sizeof(gid_txt));

        debug_write("  File: ");
        debug_write(path);
        debug_write("\n");
        snprintf(line, sizeof(line), "  Size: %s  Inode: %s  Links: %s\n", size_txt, ino_txt, nlink_txt);
        debug_write(line);
        snprintf(line, sizeof(line), "  Access: (%o)  Uid: (%s)  Gid: (%s)\n",
                 (unsigned)(st.st_mode & 07777), uid_txt, gid_txt);
        debug_write(line);
    }

    if (had_error)
        return 1;

    debug_serial_ok("stat");
    return 0;
}

struct debug_command cmd_stat = {
    .name = "stat",
    .handler = cmd_stat_handler,
    .usage = "stat PATH...",
    .description = "Display file status information"
};
