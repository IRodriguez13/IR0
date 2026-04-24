/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_mv.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: mv
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Move/rename file command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

static int cmd_mv_handler(int argc, char **argv)
{
    char msg[384];

    if (argc < 3)
    {
        debug_write_err("Usage: mv <src> <dst>\n");
        debug_serial_fail("mv", "usage");
        return -1;
    }

    const char *src = argv[1];
    const char *dst = argv[2];

    /* Try rename first (POSIX, works within same filesystem) */
    int64_t result = ir0_rename(src, dst);
    if (result == 0)
    {
        snprintf(msg, sizeof(msg), "mv: renamed '%s' -> '%s'\n", src, dst);
        debug_write(msg);
        debug_serial_ok("mv");
        return 0;
    }

    /* If rename fails, do copy + unlink */
    int src_fd = (int)syscall(SYS_OPEN, (uint64_t)src, O_RDONLY, 0);
    if (src_fd < 0)
    {
        debug_perror("mv", src, (int)src_fd);
        debug_serial_fail_err("mv", "open_src", (int)(-src_fd));
        return -1;
    }

    stat_t st;
    int64_t stat_result = syscall(SYS_FSTAT, (uint64_t)src_fd, (uint64_t)&st, 0);
    if (stat_result < 0)
    {
        syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
        debug_perror("mv", src, (int)stat_result);
        debug_serial_fail_err("mv", "fstat", (int)(-stat_result));
        return -1;
    }
    if (st.st_size < 0)
    {
        syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
        debug_perror("mv", src, EINVAL);
        debug_serial_fail_err("mv", "fstat_bad_size", EINVAL);
        return -1;
    }

    int dst_fd = (int)syscall(SYS_OPEN, (uint64_t)dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0)
    {
        syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
        debug_perror("mv", dst, (int)dst_fd);
        debug_serial_fail_err("mv", "open_dst", (int)(-dst_fd));
        return -1;
    }

    char buffer[4096];
    size_t total_copied = 0;
    size_t remaining = (size_t)st.st_size;

    while (remaining > 0)
    {
        size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        int64_t bytes_read = syscall(SYS_READ, (uint64_t)src_fd, (uint64_t)buffer, to_read);

        if (bytes_read < 0)
        {
            debug_perror("mv", src, (int)bytes_read);
            syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
            syscall(SYS_CLOSE, (uint64_t)dst_fd, 0, 0);
            debug_serial_fail_err("mv", "read", (int)(-bytes_read));
            return -1;
        }
        if (bytes_read == 0)
            break;

        int64_t bytes_written = syscall(SYS_WRITE, (uint64_t)dst_fd, (uint64_t)buffer, (size_t)bytes_read);
        if (bytes_written < 0 || bytes_written != bytes_read)
        {
            debug_perror("mv", dst, (int)bytes_written);
            syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
            syscall(SYS_CLOSE, (uint64_t)dst_fd, 0, 0);
            debug_serial_fail_err("mv", "write", bytes_written < 0 ? (int)(-bytes_written) : (int)EIO);
            return -1;
        }
        total_copied += (size_t)bytes_read;
        remaining -= (size_t)bytes_read;
    }

    syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
    syscall(SYS_CLOSE, (uint64_t)dst_fd, 0, 0);

    if (total_copied != (size_t)st.st_size)
    {
        debug_write_err("mv: incomplete copy\n");
        debug_serial_fail("mv", "incomplete");
        return -1;
    }

    int64_t unlink_result = syscall(SYS_UNLINK, (uint64_t)src, 0, 0);
    if (unlink_result < 0)
    {
        debug_perror("mv", src, (int)unlink_result);
        debug_serial_fail_err("mv", "unlink", (int)(-unlink_result));
        return -1;
    }

    snprintf(msg, sizeof(msg), "mv: moved '%s' -> '%s'\n", src, dst);
    debug_write(msg);
    debug_serial_ok("mv");
    return 0;
}

struct debug_command cmd_mv = {
    .name = "mv",
    .handler = cmd_mv_handler,
    .usage = "mv SRC DST",
    .description = "Move (rename) file"
};
