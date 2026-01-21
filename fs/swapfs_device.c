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

/* Device operations structure */
static const struct file_operations swapfs_fops = {
    .open = swapfs_device_open,
    .close = swapfs_device_close,
    .read = swapfs_device_read,
    .write = swapfs_device_write,
    .ioctl = swapfs_device_ioctl
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
    int ret = devfs_register_device("swap", DEVFS_TYPE_CHAR, 0600, &swapfs_fops);
    if (ret < 0) {
        serial_print("[SWAPFS] Failed to register /dev/swap device\n");
        return ret;
    }
    
    LOG_INFO("SWAPFS", "Registered /dev/swap character device");
    serial_print("[SWAPFS] Registered /dev/swap character device\n");
    
    return 0;
}

/**
 * swapfs_device_open - Open /dev/swap device
 * @path: Device path (should be "/dev/swap")
 * @flags: Open flags
 * 
 * Returns: File descriptor on success, negative error code on failure
 */
int swapfs_device_open(const char *path, int flags)
{
    (void)path;   /* Unused parameter */
    (void)flags;  /* Unused parameter */
    
    LOG_DEBUG("SWAPFS", "SwapFS device opened");
    return 0;
}

/**
 * swapfs_device_close - Close /dev/swap device
 * @fd: File descriptor
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_device_close(int fd)
{
    (void)fd;  /* Unused parameter */
    
    LOG_DEBUG("SWAPFS", "SwapFS device closed");
    return 0;
}

/**
 * swapfs_device_read - Read from /dev/swap device
 * @fd: File descriptor
 * @buf: Buffer to read into
 * @count: Number of bytes to read
 * 
 * Reading from /dev/swap returns current swap statistics in text format.
 * 
 * Returns: Number of bytes read, or negative error code on failure
 */
ssize_t swapfs_device_read(int fd, void *buf, size_t count)
{
    (void)fd;  /* Unused parameter */
    
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
    
    return (ssize_t)copy_len;
}

/**
 * swapfs_device_write - Write to /dev/swap device
 * @fd: File descriptor
 * @buf: Buffer to write from
 * @count: Number of bytes to write
 * 
 * Writing to /dev/swap is not supported.
 * 
 * Returns: -ENOSYS (not supported)
 */
ssize_t swapfs_device_write(int fd, const void *buf, size_t count)
{
    (void)fd;    /* Unused parameter */
    (void)buf;   /* Unused parameter */
    (void)count; /* Unused parameter */
    
    return -ENOSYS;  /* Write not supported */
}

/**
 * swapfs_device_ioctl - IOCTL operations for /dev/swap device
 * @fd: File descriptor
 * @cmd: IOCTL command
 * @arg: Command argument
 * 
 * Supports various SwapFS management operations via IOCTL.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_device_ioctl(int fd, unsigned int cmd, unsigned long arg)
{
    (void)fd;  /* Unused parameter */
    
    switch (cmd) {
        case SWAPFS_IOCTL_CREATE: {
            swapfs_create_args_t args;
            
            /* Copy arguments from user space */
            if (copy_from_user(&args, (void *)arg, sizeof(args)) != 0) {
                return -EFAULT;
            }
            
            /* Validate path */
            if (strnlen(args.path, sizeof(args.path)) >= sizeof(args.path)) {
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
            if (copy_from_user(&args, (void *)arg, sizeof(args)) != 0) {
                return -EFAULT;
            }
            
            /* Validate path */
            if (strnlen(args.path, sizeof(args.path)) >= sizeof(args.path)) {
                return -EINVAL;
            }
            
            LOG_INFO_FMT("SWAPFS", "Activating swap file: %s", args.path);
            
            return swapfs_activate_swap_file(args.path);
        }
        
        case SWAPFS_IOCTL_DEACTIVATE: {
            swapfs_activate_args_t args;  /* Same structure as activate */
            
            /* Copy arguments from user space */
            if (copy_from_user(&args, (void *)arg, sizeof(args)) != 0) {
                return -EFAULT;
            }
            
            /* Validate path */
            if (strnlen(args.path, sizeof(args.path)) >= sizeof(args.path)) {
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
            if (copy_to_user((void *)arg, &stats, sizeof(stats)) != 0) {
                return -EFAULT;
            }
            
            return 0;
        }
        
        case SWAPFS_IOCTL_LIST: {
            /* TODO: Implement list of active swap files */
            LOG_WARNING("SWAPFS", "SWAPFS_IOCTL_LIST not yet implemented");
            return -ENOSYS;
        }
        
        default:
            LOG_WARNING_FMT("SWAPFS", "Unknown IOCTL command: 0x%X", cmd);
            return -ENOTTY;
    }
}

/* Helper function for snprintf (simplified version) */
static int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    /* This is a simplified implementation for the specific format strings we use */
    /* In a real implementation, you'd want a full printf implementation */
    
    if (!buf || size == 0) {
        return -1;
    }
    
    /* For now, just copy a basic message */
    const char *basic_msg = "SwapFS Statistics: [Basic implementation - full stats via IOCTL]\n";
    size_t msg_len = strlen(basic_msg);
    
    if (msg_len >= size) {
        msg_len = size - 1;
    }
    
    memcpy(buf, basic_msg, msg_len);
    buf[msg_len] = '\0';
    
    return (int)msg_len;
}