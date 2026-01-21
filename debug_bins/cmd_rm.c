/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: rm
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Remove file or directory command (uses only syscalls)
 */

#include "debug_bins.h"
#include <string.h>

static int cmd_rm_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write_err("Usage: rm [-r] <filename>\n");
        return 1;
    }
    
    int recursive = 0;
    const char *filename = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-rf") == 0 || strcmp(argv[i], "-fr") == 0)
        {
            recursive = 1;
        }
        else if (argv[i][0] != '-')
        {
            filename = argv[i];
        }
    }
    
    if (!filename)
    {
        debug_write_err("Usage: rm [-r] <filename>\n");
        return 1;
    }
    
    int64_t result;
    
    /* If recursive flag is set, use recursive removal */
    if (recursive)
    {
        /* Try rmdir first (for directories), then unlink (for files) */
        result = ir0_rmdir(filename);
        if (result < 0)
            result = ir0_unlink(filename);
        if (result < 0)
        {
            debug_write_err("rm: cannot remove '");
            debug_write_err(filename);
            debug_write_err("': Failed to remove recursively\n");
            return 1;
        }
    }
    else
    {
        /* Try to remove as file first, then as directory */
        result = ir0_unlink(filename);
        if (result < 0)
            result = ir0_rmdir(filename);
        if (result < 0)
        {
            debug_write_err("rm: cannot remove '");
            debug_write_err(filename);
            debug_write_err("': No such file or directory\n");
            debug_write_err("Hint: Use 'rm -r' for directories\n");
            return 1;
        }
    }
    
    return 0;
}

struct debug_command cmd_rm = {
    .name = "rm",
    .handler = cmd_rm_handler,
    .usage = "rm [-r] FILE",
    .description = "Remove file or directory"
};

