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
        return 1;
    }
    
    const char *src = argv[1];
    const char *dst = argv[2];
    
    /* Open source file */
    int src_fd = syscall(SYS_OPEN, (uint64_t)src, O_RDONLY, 0);
    if (src_fd < 0)
    {
        debug_write_err("cp: cannot read source\n");
        return 1;
    }
    
    /* Get file size */
    stat_t st;
    int64_t stat_result = syscall(SYS_FSTAT, src_fd, (uint64_t)&st, 0);
    if (stat_result < 0 || st.st_size <= 0)
    {
        syscall(SYS_CLOSE, src_fd, 0, 0);
        debug_write_err("cp: cannot get source size\n");
        return 1;
    }
    
    /* Read source file */
    char buffer[4096];
    int64_t bytes_read = syscall(SYS_READ, src_fd, (uint64_t)buffer, 
                                 (st.st_size < (off_t)sizeof(buffer)) ? (size_t)st.st_size : sizeof(buffer));
    syscall(SYS_CLOSE, src_fd, 0, 0);
    
    if (bytes_read < 0)
    {
        debug_write_err("cp: read failed\n");
        return 1;
    }
    
    /* Write to destination */
    int dst_fd = syscall(SYS_OPEN, (uint64_t)dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0)
    {
        debug_write_err("cp: cannot write destination\n");
        return 1;
    }
    
    int64_t bytes_written = syscall(SYS_WRITE, dst_fd, (uint64_t)buffer, (size_t)bytes_read);
    syscall(SYS_CLOSE, dst_fd, 0, 0);
    
    if (bytes_written < 0 || bytes_written != bytes_read)
    {
        debug_write_err("cp: write failed\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_cp = {
    .name = "cp",
    .handler = cmd_cp_handler,
    .usage = "cp SRC DST",
    .description = "Copy file"
};





