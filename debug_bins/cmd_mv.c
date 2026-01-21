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
    if (argc < 3)
    {
        debug_write_err("Usage: mv <src> <dst>\n");
        return 1;
    }
    
    const char *src = argv[1];
    const char *dst = argv[2];
    
    /* Try rename first (most efficient) */
    int64_t result = syscall(SYS_LINK, (uint64_t)src, (uint64_t)dst, 0);
    if (result == 0)
    {
        /* Success: unlink source */
        syscall(SYS_UNLINK, (uint64_t)src, 0, 0);
        return 0;
    }
    
    /* If rename fails, do copy + unlink */
    /* Open source */
    int src_fd = syscall(SYS_OPEN, (uint64_t)src, O_RDONLY, 0);
    if (src_fd < 0)
    {
        debug_write_err("mv: cannot read source\n");
        return 1;
    }
    
    /* Get size */
    stat_t st;
    int64_t stat_result = syscall(SYS_FSTAT, src_fd, (uint64_t)&st, 0);
    if (stat_result < 0 || st.st_size <= 0)
    {
        syscall(SYS_CLOSE, src_fd, 0, 0);
        debug_write_err("mv: cannot get source size\n");
        return 1;
    }
    
    /* Read source */
    char buffer[4096];
    int64_t bytes_read = syscall(SYS_READ, src_fd, (uint64_t)buffer,
                                 (st.st_size < (off_t)sizeof(buffer)) ? (size_t)st.st_size : sizeof(buffer));
    syscall(SYS_CLOSE, src_fd, 0, 0);
    
    if (bytes_read < 0)
    {
        debug_write_err("mv: read failed\n");
        return 1;
    }
    
    /* Write destination */
    int dst_fd = syscall(SYS_OPEN, (uint64_t)dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0)
    {
        debug_write_err("mv: cannot write destination\n");
        return 1;
    }
    
    int64_t bytes_written = syscall(SYS_WRITE, dst_fd, (uint64_t)buffer, (size_t)bytes_read);
    syscall(SYS_CLOSE, dst_fd, 0, 0);
    
    if (bytes_written < 0 || bytes_written != bytes_read)
    {
        debug_write_err("mv: write failed\n");
        return 1;
    }
    
    /* Unlink source */
    int64_t unlink_result = syscall(SYS_UNLINK, (uint64_t)src, 0, 0);
    if (unlink_result < 0)
    {
        debug_write_err("mv: copied but failed to remove source\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_mv = {
    .name = "mv",
    .handler = cmd_mv_handler,
    .usage = "mv SRC DST",
    .description = "Move (rename) file"
};



