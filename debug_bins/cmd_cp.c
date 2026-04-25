/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_cp.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: cp
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Copy file command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

static int cp_build_temp_path(const char *dst, char *tmp_path, size_t tmp_sz)
{
    int64_t pid = syscall(SYS_GETPID, 0, 0, 0);
    char pid_str[32];
    uint64_t upid = (pid < 0) ? 0 : (uint64_t)pid;

    debug_u64_to_dec(upid, pid_str, sizeof(pid_str));
    int n = snprintf(tmp_path, tmp_sz, "%s.tmp.cp.%s", dst, pid_str);
    if (n <= 0 || n >= (int)tmp_sz)
        return -ENAMETOOLONG;
    return 0;
}

static int cmd_cp_handler(int argc, char **argv)
{
    char msg[384];
    int force = 0;
    int verbose = 0;
    const char *src = NULL;
    const char *dst = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-f") == 0)
            force = 1;
        else if (strcmp(argv[i], "-v") == 0)
            verbose = 1;
        else if (argv[i][0] == '-')
        {
            debug_write_err("cp: unknown option\n");
            debug_serial_fail("cp", "usage");
            return -1;
        }
        else if (!src)
            src = argv[i];
        else if (!dst)
            dst = argv[i];
        else
        {
            debug_write_err("cp: too many operands\n");
            debug_serial_fail("cp", "usage");
            return -1;
        }
    }

    if (!src || !dst)
    {
        debug_write_err("Usage: cp [-f] [-v] <src> <dst>\n");
        debug_serial_fail("cp", "usage");
        return -1;
    }

    int src_fd = (int)syscall(SYS_OPEN, (uint64_t)src, O_RDONLY, 0);
    if (src_fd < 0)
    {
        debug_perror("cp", src, (int)src_fd);
        debug_serial_fail_err("cp", "open_src", (int)(-src_fd));
        return -1;
    }

    stat_t st;
    int64_t stat_result = syscall(SYS_FSTAT, (uint64_t)src_fd, (uint64_t)&st, 0);
    if (stat_result < 0 || !S_ISREG(st.st_mode))
    {
        syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
        debug_perror("cp", src, stat_result < 0 ? (int)stat_result : EINVAL);
        debug_serial_fail_err("cp", "fstat", stat_result < 0 ? (int)(-stat_result) : EINVAL);
        return -1;
    }

    stat_t dst_st;
    if (syscall(SYS_STAT, (uint64_t)dst, (uint64_t)&dst_st, 0) == 0 &&
        dst_st.st_ino == st.st_ino && dst_st.st_dev == st.st_dev)
    {
        syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
        debug_write_err("cp: source and destination are the same file\n");
        debug_serial_fail("cp", "same_file");
        return -1;
    }

    char tmp_dst[512];
    if (cp_build_temp_path(dst, tmp_dst, sizeof(tmp_dst)) < 0)
    {
        syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
        debug_write_err("cp: destination path too long\n");
        debug_serial_fail("cp", "tmp_path");
        return -1;
    }

    (void)syscall(SYS_UNLINK, (uint64_t)tmp_dst, 0, 0);
    int dst_fd = (int)syscall(SYS_OPEN, (uint64_t)tmp_dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0)
    {
        syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
        debug_perror("cp", tmp_dst, (int)dst_fd);
        debug_serial_fail_err("cp", "open_dst", (int)(-dst_fd));
        return -1;
    }

    char buffer[4096];
    size_t total_copied = 0;

    while (1)
    {
        int64_t bytes_read = syscall(SYS_READ, (uint64_t)src_fd, (uint64_t)buffer, sizeof(buffer));

        if (bytes_read < 0)
        {
            debug_perror("cp", src, (int)bytes_read);
            syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
            syscall(SYS_CLOSE, (uint64_t)dst_fd, 0, 0);
            (void)syscall(SYS_UNLINK, (uint64_t)tmp_dst, 0, 0);
            debug_serial_fail_err("cp", "read", (int)(-bytes_read));
            return -1;
        }
        if (bytes_read == 0)
            break;

        int64_t bytes_written = syscall(SYS_WRITE, (uint64_t)dst_fd, (uint64_t)buffer, (size_t)bytes_read);
        if (bytes_written < 0 || bytes_written != bytes_read)
        {
            debug_perror("cp", tmp_dst, (int)bytes_written);
            syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
            syscall(SYS_CLOSE, (uint64_t)dst_fd, 0, 0);
            (void)syscall(SYS_UNLINK, (uint64_t)tmp_dst, 0, 0);
            debug_serial_fail_err("cp", "write", bytes_written < 0 ? (int)(-bytes_written) : (int)EIO);
            return -1;
        }
        total_copied += (size_t)bytes_read;
    }

    syscall(SYS_CLOSE, (uint64_t)src_fd, 0, 0);
    syscall(SYS_CLOSE, (uint64_t)dst_fd, 0, 0);

    if ((uint64_t)total_copied != (uint64_t)st.st_size)
    {
        debug_write_err("cp: incomplete copy\n");
        (void)syscall(SYS_UNLINK, (uint64_t)tmp_dst, 0, 0);
        debug_serial_fail("cp", "incomplete");
        return -1;
    }

    if (syscall(SYS_CHMOD, (uint64_t)tmp_dst, (uint64_t)(st.st_mode & 07777), 0) < 0)
    {
        debug_write_err("cp: copied but could not preserve mode\n");
    }

    int64_t ren = syscall(SYS_RENAME, (uint64_t)tmp_dst, (uint64_t)dst, 0);
    if (ren < 0 && force)
    {
        (void)syscall(SYS_UNLINK, (uint64_t)dst, 0, 0);
        ren = syscall(SYS_RENAME, (uint64_t)tmp_dst, (uint64_t)dst, 0);
    }

    if (ren < 0)
    {
        debug_perror("cp", dst, (int)ren);
        (void)syscall(SYS_UNLINK, (uint64_t)tmp_dst, 0, 0);
        debug_serial_fail("cp", "rename");
        return -1;
    }

    if (verbose)
    {
        snprintf(msg, sizeof(msg), "cp: copied '%s' -> '%s' (%u bytes)\n",
                 src, dst, (unsigned)total_copied);
        debug_write(msg);
    }
    else
    {
        snprintf(msg, sizeof(msg), "cp: copied '%s' -> '%s'\n", src, dst);
        debug_write(msg);
    }

    debug_serial_ok("cp");
    return 0;
}

struct debug_command cmd_cp = {
    .name = "cp",
    .handler = cmd_cp_handler,
    .usage = "cp [-f] [-v] SRC DST",
    .description = "Copy file"
};
