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
#include <ir0/ioctl.h>

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
    
    // Open the swap control device
    int ctl_fd = sys_open("/dev/swap", O_RDWR, 0);
    if (ctl_fd < 0) {
        debug_writeln_err("Error: Failed to open swap control device");
        return 1;
    }
    
    // Open the swap file
    int file_fd = sys_open(swap_file, O_RDWR, 0);
    if (file_fd < 0) {
        sys_close(ctl_fd);
        debug_write_err("Error: Failed to open swap file: ");
        debug_writeln_err(swap_file);
        return 1;
    }
    
    // Use ioctl to add the swap file
    int ret = sys_ioctl(ctl_fd, SWAPFS_IOCTL_ADD, (void*)(uintptr_t)file_fd);
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
    
    sys_close(file_fd);
    sys_close(ctl_fd);
    return ret < 0 ? 1 : 0;
}

struct debug_command cmd_swapon = {
    .name = "swapon",
    .handler = cmd_swapon_handler,
    .description = "Enable swap on a file",
    .usage = "swapon <file>"
};