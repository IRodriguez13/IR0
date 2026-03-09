/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: swapfs_device.c
 * Description: SwapFS device interface - /dev/swap character device
 */

#include "swapfs.h"
#include <ir0/devfs.h>
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <drivers/serial/serial.h>
#include <ir0/copy_user.h>
#include <string.h>

static int64_t swapfs_dev_open(devfs_entry_t *entry, int flags);
static int64_t swapfs_dev_close(devfs_entry_t *entry);
static int64_t swapfs_dev_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset);
static int64_t swapfs_dev_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset);
static int64_t swapfs_dev_ioctl(devfs_entry_t *entry, uint64_t request, void *arg);

static const devfs_ops_t swapfs_ops = {
    .open = swapfs_dev_open,
    .close = swapfs_dev_close,
    .read = swapfs_dev_read,
    .write = swapfs_dev_write,
    .ioctl = swapfs_dev_ioctl
};

/**
 * swapfs_device_init - Initialize SwapFS device interface
 * 
 * Creates the /dev/swap character device for user-space interaction
 * with the SwapFS subsystem.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_device_init(void)
{
    /* Register /dev/swap character device */
    int ret = devfs_register_device("swap", &swapfs_ops, 0600);
    if (ret < 0) {
        serial_print("[SWAPFS] Failed to register /dev/swap device\n");
        return ret;
    }
    
    LOG_INFO("SWAPFS", "Registered /dev/swap character device");
    serial_print("[SWAPFS] Registered /dev/swap character device\n");
    
    return 0;
}

static int64_t swapfs_dev_open(devfs_entry_t *entry, int flags)
{
    (void)entry;
    (void)flags;
    LOG_DEBUG("SWAPFS", "SwapFS device opened");
    return 0;
}

static int64_t swapfs_dev_close(devfs_entry_t *entry)
{
    (void)entry;
    LOG_DEBUG("SWAPFS", "SwapFS device closed");
    return 0;
}

/**
 * swapfs_dev_read - Read from /dev/swap device
 * Returns current swap statistics in text format.
 */
static int64_t swapfs_dev_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)offset;
    
    if (!buf || count == 0) {
        return -EINVAL;
    }
    
    /* Get current swap statistics */
    swapfs_stats_t stats;
    int ret = swapfs_get_stats(&stats);
    if (ret < 0) {
        return ret;
    }
    
    /* Format statistics as text */
    char stats_text[512];
    int len = snprintf(stats_text, sizeof(stats_text),
        "SwapFS Statistics:\n"
        "  Active swap files: %u\n"
        "  Total swap size: %llu bytes (%llu KB)\n"
        "  Used swap size: %llu bytes (%llu KB)\n"
        "  Free swap size: %llu bytes (%llu KB)\n"
        "  Pages swapped in: %llu\n"
        "  Pages swapped out: %llu\n"
        "  Total swap operations: %llu\n"
        "  Swap utilization: %.1f%%\n",
        stats.total_swap_files,
        stats.total_swap_size, stats.total_swap_size / 1024,
        stats.used_swap_size, stats.used_swap_size / 1024,
        stats.total_swap_size - stats.used_swap_size,
        (stats.total_swap_size - stats.used_swap_size) / 1024,
        stats.pages_swapped_in,
        stats.pages_swapped_out,
        stats.swap_operations,
        stats.total_swap_size > 0 ? 
            (double)stats.used_swap_size * 100.0 / stats.total_swap_size : 0.0
    );
    
    if (len < 0) {
        return -EIO;
    }
    
    /* Copy to user buffer */
    size_t copy_len = (size_t)len < count ? (size_t)len : count;
    if (copy_to_user(buf, stats_text, copy_len) != 0) {
        return -EFAULT;
    }
    
    return (int64_t)copy_len;
}

static int64_t swapfs_dev_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    (void)buf;
    (void)count;
    (void)offset;
    return -ENOSYS;
}

static int64_t swapfs_dev_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    (void)entry;
    unsigned int cmd = (unsigned int)request;
    unsigned long arg_val = (unsigned long)(uintptr_t)arg;

    switch (cmd) {
        case SWAPFS_IOCTL_CREATE: {
            swapfs_create_args_t args;
            
            /* Copy arguments from user space */
            if (copy_from_user(&args, (void *)arg_val, sizeof(args)) != 0) {
                return -EFAULT;
            }
            
            /* Validate path is null-terminated */
            {
                size_t plen = 0;
                while (plen < sizeof(args.path) && args.path[plen]) plen++;
                if (plen >= sizeof(args.path))
                    return -EINVAL;
            }
            
            /* Validate size */
            if (args.size_mb == 0 || args.size_mb > 1024) {  /* Max 1GB */
                return -EINVAL;
            }
            
            LOG_INFO_FMT("SWAPFS", "Creating swap file: %s (%zu MB)", 
                         args.path, args.size_mb);
            
            return swapfs_create_swap_file(args.path, args.size_mb);
        }
        
        case SWAPFS_IOCTL_ACTIVATE: {
            swapfs_activate_args_t args;
            
            /* Copy arguments from user space */
            if (copy_from_user(&args, (void *)arg_val, sizeof(args)) != 0) {
                return -EFAULT;
            }
            
            /* Validate path is null-terminated */
            {
                size_t plen = 0;
                while (plen < sizeof(args.path) && args.path[plen]) plen++;
                if (plen >= sizeof(args.path))
                    return -EINVAL;
            }
            
            LOG_INFO_FMT("SWAPFS", "Activating swap file: %s", args.path);
            
            return swapfs_activate_swap_file(args.path);
        }
        
        case SWAPFS_IOCTL_DEACTIVATE: {
            swapfs_activate_args_t args;  /* Same structure as activate */
            
            /* Copy arguments from user space */
            if (copy_from_user(&args, (void *)arg_val, sizeof(args)) != 0) {
                return -EFAULT;
            }
            
            /* Validate path is null-terminated */
            {
                size_t plen = 0;
                while (plen < sizeof(args.path) && args.path[plen]) plen++;
                if (plen >= sizeof(args.path))
                    return -EINVAL;
            }
            
            LOG_INFO_FMT("SWAPFS", "Deactivating swap file: %s", args.path);
            
            return swapfs_deactivate_swap_file(args.path);
        }
        
        case SWAPFS_IOCTL_STATS: {
            swapfs_stats_t stats;
            
            /* Get current statistics */
            int ret = swapfs_get_stats(&stats);
            if (ret < 0) {
                return ret;
            }
            
            /* Copy statistics to user space */
            if (copy_to_user((void *)arg_val, &stats, sizeof(stats)) != 0) {
                return -EFAULT;
            }
            
            return 0;
        }
        
        case SWAPFS_IOCTL_LIST: {
            swapfs_list_t list;
            int ret = swapfs_get_active_list(&list);
            if (ret < 0)
                return ret;
            if (copy_to_user((void *)arg_val, &list, sizeof(list)) != 0)
                return -EFAULT;
            return 0;
        }
        
        default:
            LOG_WARNING_FMT("SWAPFS", "Unknown IOCTL command: 0x%X", cmd);
            return -ENOTTY;
    }
}
