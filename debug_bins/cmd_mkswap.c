/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: mkswap
 * Crear archivo de swap usando solo syscalls (open/ioctl/close a /dev/swap).
 */

#include "debug_bins.h"
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <string.h>
#include <stdlib.h>

/* IOCTL /dev/swap (sin incluir fs/swapfs.h). */
#define SWAPFS_IOCTL_CREATE  0x5301

static int cmd_mkswap_handler(int argc, char **argv)
{
    if (argc < 2)
    {
        debug_writeln_err("Usage: mkswap <file> [size_mb]");
        debug_writeln_err("       Default size is 64 MB if not specified");
        return 1;
    }

    const char *swap_file = argv[1];
    size_t size_mb = 64;

    if (argc >= 3)
    {
        const char *p = argv[2];
        size_mb = 0;
        while (*p >= '0' && *p <= '9')
            size_mb = size_mb * 10 + (size_t)(*p++ - '0');
        if (size_mb == 0)
        {
            debug_writeln_err("mkswap: Invalid size specified");
            return 1;
        }
        if (size_mb > 1024)
        {
            debug_writeln_err("mkswap: Maximum swap file size is 1024 MB");
            return 1;
        }
    }

    debug_write("Creating swap file: ");
    debug_writeln(swap_file);

    int ctl_fd = (int)ir0_open("/dev/swap", O_RDWR, 0);
    if (ctl_fd < 0)
    {
        debug_writeln_err("mkswap: cannot open /dev/swap");
        return 1;
    }

    struct { char path[256]; size_t size_mb; } args;
    size_t i = 0;
    while (swap_file[i] && i < sizeof(args.path) - 1)
        args.path[i] = swap_file[i++];
    args.path[i] = '\0';
    args.size_mb = size_mb;

    int ret = (int)ir0_ioctl(ctl_fd, SWAPFS_IOCTL_CREATE, &args);
    ir0_close(ctl_fd);

    if (ret < 0)
    {
        debug_write_err("mkswap: failed ");
        switch (-ret)
        {
            case ENODEV: debug_writeln_err("(SwapFS not initialized)"); break;
            case EINVAL: debug_writeln_err("(Invalid parameters)"); break;
            case EEXIST: debug_writeln_err("(File already exists as swap)"); break;
            case ENOSPC: debug_writeln_err("(No space left)"); break;
            case EIO:    debug_writeln_err("(I/O error)"); break;
            case EACCES: debug_writeln_err("(Permission denied)"); break;
            default:     debug_writeln_err("(unknown error)"); break;
        }
        return 1;
    }

    debug_writeln("Swap file created successfully.");
    debug_write("To enable: swapon ");
    debug_writeln(swap_file);
    return 0;
}

struct debug_command cmd_mkswap = {
    .name = "mkswap",
    .handler = cmd_mkswap_handler,
    .usage = "mkswap <file> [size_mb]",
    .description = "Create swap file"
};
