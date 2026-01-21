/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: sed
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Stream editor command using POSIX syscalls only (basic substitution)
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>

static int cmd_sed_handler(int argc, char **argv)
{
    if (argc < 3)
    {
        debug_write_err("Usage: sed 's/OLD/NEW/' FILE\n");
        return 1;
    }
    
    const char *cmd = argv[1];
    const char *filename = argv[2];
    
    /* Parse substitution command: s/OLD/NEW/ */
    if (cmd[0] != 's' || cmd[1] != '/')
    {
        debug_write_err("sed: only 's/OLD/NEW/' substitution supported\n");
        return 1;
    }
    
    const char *old_str_start = cmd + 2;
    const char *slash = strchr(old_str_start, '/');
    if (!slash)
    {
        debug_write_err("sed: invalid substitution syntax\n");
        return 1;
    }
    
    const char *new_str_start = slash + 1;
    const char *end_slash = strchr(new_str_start, '/');
    if (!end_slash)
    {
        debug_write_err("sed: invalid substitution syntax\n");
        return 1;
    }
    
    /* Extract OLD and NEW strings */
    size_t old_len = (size_t)(slash - old_str_start);
    size_t new_len = (size_t)(end_slash - new_str_start);
    
    if (old_len == 0 || old_len >= 256 || new_len >= 256)
    {
        debug_write_err("sed: string too long\n");
        return 1;
    }
    
    char old_str[256];
    char new_str[256];
    strncpy(old_str, old_str_start, old_len);
    old_str[old_len] = '\0';
    strncpy(new_str, new_str_start, new_len);
    new_str[new_len] = '\0';
    
    /* Read file */
    int fd = syscall(SYS_OPEN, (uint64_t)filename, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_write_err("sed: cannot open file\n");
        return 1;
    }
    
    char buffer[4096];
    int64_t bytes_read = syscall(SYS_READ, fd, (uint64_t)buffer, sizeof(buffer) - 1);
    syscall(SYS_CLOSE, fd, 0, 0);
    
    if (bytes_read < 0)
    {
        debug_write_err("sed: read failed\n");
        return 1;
    }
    
    buffer[bytes_read] = '\0';
    
    /* Perform substitution */
    char *result = strstr(buffer, old_str);
    if (result)
    {
        /* Simple substitution: replace first occurrence */
        size_t prefix_len = (size_t)(result - buffer);
        size_t suffix_len = strlen(result + old_len);
        size_t new_size = prefix_len + new_len + suffix_len;
        
        if (new_size < sizeof(buffer))
        {
            char new_buffer[4096];
            strncpy(new_buffer, buffer, prefix_len);
            new_buffer[prefix_len] = '\0';
            strcat(new_buffer, new_str);
            strcat(new_buffer, result + old_len);
            
            /* Write back */
            fd = syscall(SYS_OPEN, (uint64_t)filename, O_WRONLY | O_TRUNC, 0);
            if (fd >= 0)
            {
                syscall(SYS_WRITE, fd, (uint64_t)new_buffer, strlen(new_buffer));
                syscall(SYS_CLOSE, fd, 0, 0);
            }
        }
    }
    
    return 0;
}

struct debug_command cmd_sed = {
    .name = "sed",
    .handler = cmd_sed_handler,
    .usage = "sed 's/OLD/NEW/' FILE",
    .description = "Substitute text in file"
};



