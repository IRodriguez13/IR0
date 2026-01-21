/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: echo
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Echo command using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/fcntl.h>
#include <string.h>

static int cmd_echo_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_write("\n");
        return 0;
    }
    
    /* Check for redirection: >> or > */
    int redir_append = 0;
    int redir_overwrite = 0;
    const char *filename = NULL;
    int last_arg = argc;
    
    /* Check last arguments for redirection */
    for (int i = argc - 1; i >= 1; i--)
    {
        if (strcmp(argv[i], ">>") == 0 && i + 1 < argc)
        {
            redir_append = 1;
            filename = argv[i + 1];
            last_arg = i;
            break;
        }
        else if (argv[i][0] == '>' && strlen(argv[i]) == 1 && i + 1 < argc)
        {
            redir_overwrite = 1;
            filename = argv[i + 1];
            last_arg = i;
            break;
        }
    }
    
    /* Build message from all args before redirection */
    char message[1024] = "";
    for (int i = 1; i < last_arg; i++)
    {
        if (i > 1)
            strcat(message, " ");
        strncat(message, argv[i], sizeof(message) - strlen(message) - 1);
    }
    strncat(message, "\n", sizeof(message) - strlen(message) - 1);
    
    if (filename)
    {
        /* Write to file */
        int flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (redir_append)
        {
            flags = O_WRONLY | O_CREAT | O_APPEND;
        }
        
        int fd = syscall(SYS_OPEN, (uint64_t)filename, flags, 0644);
        if (fd < 0)
        {
            debug_write_err("echo: cannot open file '");
            debug_write_err(filename);
            debug_write_err("'\n");
            return 1;
        }
        
        size_t msg_len = strlen(message);
        int64_t result = syscall(SYS_WRITE, fd, (uint64_t)message, msg_len);
        syscall(SYS_CLOSE, fd, 0, 0);
        
        if (result < 0)
        {
            debug_write_err("echo: write failed\n");
            return 1;
        }
    }
    else
    {
        /* Print to stdout */
        debug_write(message);
    }
    
    return 0;
}

struct debug_command cmd_echo = {
    .name = "echo",
    .handler = cmd_echo_handler,
    .usage = "echo [TEXT] [> FILE] [>> FILE]",
    .description = "Print text or write to file"
};



