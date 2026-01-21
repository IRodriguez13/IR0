/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: cat
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Print file contents command (uses only syscalls)
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>

static int cmd_cat_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: cat <filename>\n");
        return 1;
    }
    
    const char *filename = argv[1];
    int fd = ir0_open(filename, O_RDONLY, 0);
    if (fd < 0)
    {
        debug_write_err("cat: cannot open '");
        debug_write_err(filename);
        debug_write_err("'\n");
        return 1;
    }
    
    char buffer[512];
    int max_iterations = 1000; /* Safety limit */
    int iteration = 0;
    
    for (;;)
    {
        if (iteration >= max_iterations)
        {
            debug_write_err("cat: too many iterations, possible infinite loop\n");
            break;
        }
        
        int64_t bytes_read = ir0_read(fd, buffer, sizeof(buffer));
        
        if (bytes_read <= 0)
            break;
        
        syscall(SYS_WRITE, 1, (uint64_t)buffer, (uint64_t)bytes_read);
        iteration++;
    }
    
    ir0_close(fd);
    return 0;
}

struct debug_command cmd_cat = {
    .name = "cat",
    .handler = cmd_cat_handler,
    .usage = "cat FILE",
    .description = "Print file contents"
};

