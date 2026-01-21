/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: cmd_mkswap.c
 * Description: mkswap command - Create swap files
 */

#include "dbgshell.h"
#include <fs/swapfs.h>
#include <ir0/vga.h>
#include <string.h>
#include <stdlib.h>

/**
 * cmd_mkswap - Create a swap file
 * @argc: Number of arguments
 * @argv: Argument array
 * 
 * Usage: mkswap <file> [size_mb]
 * 
 * If size_mb is not specified, defaults to 64 MB.
 * 
 * Returns: 0 on success, non-zero on error
 */
int cmd_mkswap(int argc, char **argv)
{
    if (argc < 2) {
        print_colored("Usage: mkswap <file> [size_mb]\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        print_colored("       Default size is 64 MB if not specified\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        return 1;
    }
    
    const char *swap_file = argv[1];
    size_t size_mb = 64;  /* Default size */
    
    /* Parse size if provided */
    if (argc >= 3) {
        /* Simple atoi implementation for size parsing */
        const char *size_str = argv[2];
        size_mb = 0;
        
        while (*size_str >= '0' && *size_str <= '9') {
            size_mb = size_mb * 10 + (*size_str - '0');
            size_str++;
        }
        
        if (size_mb == 0) {
            print_colored("Error: Invalid size specified\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
            return 1;
        }
        
        if (size_mb > 1024) {
            print_colored("Error: Maximum swap file size is 1024 MB\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
            return 1;
        }
    }
    
    print_colored("Creating swap file: ", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored(swap_file, VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored(" (", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_int64(size_mb);
    print_colored(" MB)\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    print_colored("This may take a moment...\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    int ret = swapfs_create_swap_file(swap_file, size_mb);
    if (ret < 0) {
        print_colored("Error: Failed to create swap file ", VGA_COLOR_RED, VGA_COLOR_BLACK);
        print_colored(swap_file, VGA_COLOR_RED, VGA_COLOR_BLACK);
        
        switch (ret) {
            case -ENODEV:
                print_colored(" (SwapFS not initialized)\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
                break;
            case -EINVAL:
                print_colored(" (Invalid parameters)\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
                break;
            case -EEXIST:
                print_colored(" (File already exists as swap file)\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
                break;
            case -ENOSPC:
                print_colored(" (No space left on device)\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
                break;
            case -EIO:
                print_colored(" (I/O error)\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
                break;
            case -EACCES:
                print_colored(" (Permission denied)\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
                break;
            default:
                print_colored(" (Unknown error: ");
                print_int32(ret);
                print_colored(")\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
                break;
        }
        return 1;
    }
    
    print_colored("Swap file created successfully\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    /* Calculate and display some statistics */
    uint32_t total_pages = (size_mb * 1024 * 1024 - sizeof(swapfs_header_t)) / 4096;
    
    print_colored("Swap file details:\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("  File: ", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored(swap_file, VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    print_colored("  Size: ", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_int64(size_mb);
    print_colored(" MB (", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_int32(total_pages);
    print_colored(" pages)\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    print_colored("  Page size: 4096 bytes\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    print_colored("\nTo enable this swap file, use: swapon ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print_colored(swap_file, VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    return 0;
}

struct debug_command cmd_mkswap = {
    .name = "mkswap",
    .handler = cmd_mkswap,
};