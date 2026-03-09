/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: swapon
 * Copyright (C) 2026 Iván Rodríguez
 *
 * Enable swap on a file using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <string.h>

/* IOCTL /dev/swap: activar por path (sin incluir fs/swapfs.h). */
#define SWAPFS_IOCTL_ACTIVATE  0x5302

/**
 * cmd_swapon - Enable swap on a file
 * @argc: Number of arguments
 * @argv: Argument array
 * 
 * Usage: swapon <file>
 * 
 * Returns: 0 on success, non-zero on error
 */
static int cmd_swapon_handler(int argc, char **argv)
{
    if (argc < 2) {
        debug_writeln_err("Usage: swapon <file>");
        return 1;
    }

    const char *swap_file = argv[1];
    
    debug_write("Enabling swap on: ");
    debug_writeln(swap_file);
    
    /* Solo syscalls: abrir /dev/swap e ioctl ACTIVATE con path. */
    int ctl_fd = (int)ir0_open("/dev/swap", O_RDWR, 0);
    if (ctl_fd < 0) {
        debug_writeln_err("Error: Failed to open swap control device");
        return 1;
    }

    struct { char path[256]; } args;
    size_t len = 0;
    while (swap_file[len] && len < sizeof(args.path) - 1)
        args.path[len] = swap_file[len++];
    args.path[len] = '\0';

    int ret = (int)ir0_ioctl(ctl_fd, SWAPFS_IOCTL_ACTIVATE, &args);
    if (ret < 0) {
        debug_write_err("Error: Failed to enable swap: ");
        switch (-ret) {
            case EINVAL: debug_writeln_err("Invalid file format"); break;
            case EEXIST: debug_writeln_err("Swap file already active"); break;
            case ENOMEM: debug_writeln_err("Out of memory"); break;
            case EIO:    debug_writeln_err("I/O error"); break;
            default:     debug_writeln_err("Unknown error"); break;
        }
    } else {
        debug_writeln("Swap enabled successfully");
    }

    ir0_close(ctl_fd);
    return ret < 0 ? 1 : 0;
}

struct debug_command cmd_swapon = {
    .name = "swapon",
    .handler = cmd_swapon_handler,
    .description = "Enable swap on a file",
    .usage = "swapon <file>"
};