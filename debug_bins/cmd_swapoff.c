/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: swapoff
 * Copyright (C) 2026 Iván Rodríguez
 *
 * Disable swap on a file using POSIX syscalls only
 */

#include "debug_bins.h"
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/ioctl.h>
#include <string.h>

/* IOCTL command for swap deactivation */
#define SWAPFS_IOCTL_DEACTIVATE  0x5303

/* Structure for deactivation argument */
typedef struct {
    char path[256];
} swapfs_activate_args_t;

/**
 * cmd_swapoff - Disable swap on a file or all swap files
 * @argc: Number of arguments
 * @argv: Argument array
 * 
 * Usage: swapoff <file>
 *        swapoff -a (disable all swap files)
 * 
 * Returns: 0 on success, non-zero on error
 */
static int cmd_swapoff_handler(int argc, char **argv)
{
    if (argc < 2) {
        debug_writeln_err("Usage: swapoff <file>");
        debug_writeln_err("       swapoff -a (disable all)");
        return 1;
    }
    
    /* Handle -a flag (disable all) */
    if (argc == 2 && strcmp(argv[1], "-a") == 0) {
        debug_writeln("Disabling all swap files...");
        
        /* Open the swap control device */
        int ctl_fd = sys_open("/dev/swap", O_RDWR, 0);
        if (ctl_fd < 0) {
            debug_writeln_err("Error: Failed to open swap control device");
            return 1;
        }
        
        /* Deactivate all swap files */
        int ret = sys_ioctl(ctl_fd, SWAPFS_IOCTL_DEACTIVATE, 1); /* 1 = deactivate all */
        sys_close(ctl_fd);
        
        if (ret < 0) {
            debug_write_err("Error: Failed to disable swap: ");
            switch (-ret) {
                case ENODEV: debug_writeln_err("No swap devices active"); break;
                case EPERM:  debug_writeln_err("Permission denied"); break;
                case EBUSY:  debug_writeln_err("Swap in use, cannot deactivate"); break;
                default:     debug_writeln_err("Unknown error"); break;
            }
            return 1;
        }
        
        debug_writeln("All swap files disabled successfully");
        return 0;
    }
    
    /* Disable swap on specified file */
    const char *swap_file = argv[1];
    
    debug_write("Disabling swap on: ");
    debug_writeln(swap_file);
    
    // Open the swap control device
    int ctl_fd = sys_open("/dev/swap", O_RDWR, 0);
    if (ctl_fd < 0) {
        debug_writeln_err("Error: Failed to open swap control device");
        return 1;
    }
    
    // Prepare the deactivation argument
    swapfs_activate_args_t deactivate_arg;
    strncpy(deactivate_arg.path, swap_file, sizeof(deactivate_arg.path) - 1);
    deactivate_arg.path[sizeof(deactivate_arg.path) - 1] = '\0';
    
    // Use ioctl to deactivate the swap file
    int ret = sys_ioctl(ctl_fd, SWAPFS_IOCTL_DEACTIVATE, (unsigned long)&deactivate_arg);
    sys_close(ctl_fd);
    
    if (ret < 0) {
        debug_write_err("Error: Failed to disable swap: ");
        switch (-ret) {
            case ENOENT: debug_writeln_err("Swap file not active"); break;
            case EINVAL: debug_writeln_err("Invalid argument or path too long"); break;
            case EBUSY:  debug_writeln_err("Swap file is in use"); break;
            case EIO:    debug_writeln_err("I/O error"); break;
            case ENODEV: debug_writeln_err("Swap device not available"); break;
            case EPERM:  debug_writeln_err("Permission denied"); break;
            default:     debug_writeln_err("Unknown error"); break;
        }
        return 1;
    }
    
    debug_writeln("Swap disabled successfully");
    return 0;
}

struct debug_command cmd_swapoff = {
    .name = "swapoff",
    .handler = cmd_swapoff_handler,
    .description = "Disable swap on a file or all swap files",
    .usage = "swapoff <file> | swapoff -a"
};