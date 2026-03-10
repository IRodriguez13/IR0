/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: cp
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Copy file command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>

static int cmd_cp_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("Usage: cp <src> <dst>\n");
        debug_serial_fail("cp", "usage");
        return 1;
    }
    
    const char *src = argv[1];
    const char *dst = argv[2];
    
    /* Open source file */
    int src_fd = syscall(SYS_OPEN, (uint64_t)src, O_RDONLY, 0);
    if (src_fd < 0)
    {
        debug_perror("cp", src, (int)src_fd);
        debug_serial_fail_err("cp", "open_src", (int)(-src_fd));
        return 1;
    }
    
    /* Get file size */
    stat_t st;
    int64_t stat_result = syscall(SYS_FSTAT, src_fd, (uint64_t)&st, 0);
    if (stat_result < 0 || st.st_size < 0)
    {
        syscall(SYS_CLOSE, src_fd, 0, 0);
        debug_perror("cp", src, (int)stat_result);
        debug_serial_fail_err("cp", "fstat", (int)(-stat_result));
        return 1;
    }
    
    /* Open destination before closing source (allow same-file copy) */
    int dst_fd = syscall(SYS_OPEN, (uint64_t)dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0)
    {
        syscall(SYS_CLOSE, src_fd, 0, 0);
        debug_perror("cp", dst, (int)dst_fd);
        debug_serial_fail_err("cp", "open_dst", (int)(-dst_fd));
        return 1;
    }
    
    /* Copy in chunks (OSDev VFS: read/write loop for arbitrary file size) */
    char buffer[4096];
    size_t total_copied = 0;
    size_t remaining = (size_t)st.st_size;
    while (remaining > 0)
    {
        size_t to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        int64_t bytes_read = syscall(SYS_READ, src_fd, (uint64_t)buffer, to_read);
        if (bytes_read <= 0)
            break;
        int64_t bytes_written = syscall(SYS_WRITE, dst_fd, (uint64_t)buffer, (size_t)bytes_read);
        if (bytes_written < 0 || bytes_written != bytes_read)
        {
            debug_perror("cp", dst, (int)bytes_written);
            syscall(SYS_CLOSE, src_fd, 0, 0);
            syscall(SYS_CLOSE, dst_fd, 0, 0);
            debug_serial_fail_err("cp", "write", bytes_written < 0 ? (int)(-bytes_written) : (int)EIO);
            return 1;
        }
        total_copied += (size_t)bytes_read;
        remaining -= (size_t)bytes_read;
    }
    syscall(SYS_CLOSE, src_fd, 0, 0);
    syscall(SYS_CLOSE, dst_fd, 0, 0);
    
    if (total_copied != (size_t)st.st_size)
    {
        debug_write_err("cp: incomplete copy\n");
        debug_serial_fail("cp", "incomplete");
        return 1;
    }
    
    debug_serial_ok("cp");
    return 0;
}

struct debug_command cmd_cp = {
    .name = "cp",
    .handler = cmd_cp_handler,
    .usage = "cp SRC DST",
    .description = "Copy file"
};





