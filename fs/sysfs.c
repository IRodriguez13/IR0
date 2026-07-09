/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sysfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - SYSFS (System Filesystem)
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Simple /sys filesystem similar to Linux sysfs
 * Exposes kernel configuration and device information
 * 
 * Linux sysfs design principles we follow:
 * - Hierarchical device/driver representation
 * - Kernel parameters as writable files
 * - Standard file operations (read/write) for configuration
 * - Clean separation: /proc = stats, /sys = configuration
 * 
 * Implementation Notes:
 * - Uses FD range 3000-3999 for /sys file descriptors
 * - Reuses procfs offset tracking mechanism
 * - Files are dynamically generated on access
 * - Supports both read and write operations
 */

#include "procfs.h"
#include <ir0/sysfs.h>
#include <ir0/stat.h>
#include <ir0/kmem.h>
#include <ir0/vga.h>
#include <ir0/version.h>
#include <ir0/console_backend.h>
#include <string.h>
#include <ir0/errno.h>
#include <config.h>
#include <ir0/partition.h>
#include <ir0/block_dev.h>
#include <ir0/video_backend.h>
#include <ir0/pseudo_fs.h>
#include <ir0/arch_port.h>

#define SYS_BUFFER_SIZE 4096
#define SYS_FD_BASE 3000  /* sysfs uses FD range 3000-3999 */
#define SYS_DEFAULT_FILE_SIZE 256
#define SYS_MAX_CPUS 16

static char sys_kernel_hostname[64] = "ir0-kernel";

/**
 * Offset tracking for /sys files
 * 
 * Reuses procfs offset tracking mechanism.
 * sysfs uses FD range 3000-3999, procfs uses 1000-1999,
 * but both can use the same tracking mechanism.
 */

/* Check if path is in /sys */
bool is_sys_path(const char *path)
{
    return path && strncmp(path, "/sys/", 5) == 0;
}

/**
 * sys_kernel_version_read - Read kernel version from /sys/kernel/version
 * @buf: Buffer to write version string
 * @count: Size of buffer
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
int sys_kernel_version_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    int len = snprintf(buf, count, "%s\n", IR0_VERSION_STRING ? IR0_VERSION_STRING : "unknown");
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    return len;
}

/**
 * sys_kernel_hostname_read_reg - Hostname read for sysfs and pseudo_fs hooks.
 */
int sys_kernel_hostname_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    int len = snprintf(buf, count, "%s\n", sys_kernel_hostname);
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    return len;
}

/**
 * sys_kernel_hostname_write_reg - Hostname write for sysfs and pseudo_fs hooks.
 */
int sys_kernel_hostname_write_reg(const char *buf, size_t count)
{
    if (!buf || count == 0)
        return 0;

    size_t copy_len = (count < sizeof(sys_kernel_hostname) - 1) ? count : (sizeof(sys_kernel_hostname) - 1);
    memcpy(sys_kernel_hostname, buf, copy_len);
    sys_kernel_hostname[copy_len] = '\0';

    while (copy_len > 0 &&
           (sys_kernel_hostname[copy_len - 1] == '\n' ||
            sys_kernel_hostname[copy_len - 1] == '\r' ||
            sys_kernel_hostname[copy_len - 1] == ' ' ||
            sys_kernel_hostname[copy_len - 1] == '\t'))
    {
        copy_len--;
        sys_kernel_hostname[copy_len] = '\0';
    }

    if (copy_len == 0)
        return -EINVAL;

    return (int)count;
}

/**
 * sys_max_processes - Maximum number of processes allowed
 * 
 * Default value is 1024. Can be modified via /sys/kernel/max_processes
 */
static uint32_t sys_max_processes = 1024;

/**
 * sys_kernel_max_processes_read - Read maximum processes from /sys/kernel/max_processes
 * @buf: Buffer to write value
 * @count: Size of buffer
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
int sys_kernel_max_processes_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    int len = snprintf(buf, count, "%u\n", (unsigned)sys_max_processes);
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    return len;
}

/**
 * sys_kernel_max_processes_write - Write maximum processes to /sys/kernel/max_processes
 * @buf: Buffer containing new value
 * @count: Size of buffer
 * 
 * Validates and sets new maximum process limit.
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
int sys_kernel_max_processes_write_reg(const char *buf, size_t count)
{
    if (!buf || count == 0)
        return 0;
    
    /* Parse number from buffer */
    char value_buf[32];
    size_t copy_len = (count < sizeof(value_buf) - 1) ? count : (sizeof(value_buf) - 1);
    memcpy(value_buf, buf, copy_len);
    value_buf[copy_len] = '\0';
    
    /* Remove trailing whitespace */
    while (copy_len > 0 && (value_buf[copy_len - 1] == '\n' || value_buf[copy_len - 1] == '\r' || value_buf[copy_len - 1] == ' '))
    {
        copy_len--;
        value_buf[copy_len] = '\0';
    }
    
    uint32_t new_value = 0;
    for (size_t i = 0; i < copy_len; i++)
    {
        if (value_buf[i] >= '0' && value_buf[i] <= '9')
        {
            new_value = new_value * 10 + (value_buf[i] - '0');
        }
        else
        {
            return -EINVAL;  /* Invalid character */
        }
    }
    
    /* Validate range */
    if (new_value < 1 || new_value > 65535)
        return -EINVAL;
    
    sys_max_processes = new_value;
    return (int)count;
}

/**
 * sys_console_mode_read - Read console backend from /sys/console/mode
 * Returns "framebuffer WxH" or "vga" for verification
 */
int sys_console_mode_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;

    memset(buf, 0, count);

    int len;
    if (console_backend_uses_framebuffer())
    {
#if CONFIG_ENABLE_VBE
        uint32_t w, h, bpp;
        if (video_backend_get_info(&w, &h, &bpp))
            len = snprintf(buf, count, "framebuffer %ux%ux%u\n", (unsigned)w, (unsigned)h, (unsigned)bpp);
        else
            len = snprintf(buf, count, "framebuffer\n");
#else
        len = snprintf(buf, count, "framebuffer\n");
#endif
    }
    else
    {
        len = snprintf(buf, count, "vga\n");
    }
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    return len;
}

static int sys_devices_cpu_online[SYS_MAX_CPUS] = { 1 };

static uint32_t sys_sysfs_cpu_count(void)
{
    uint32_t n = arch_get_cpu_count();

    if (n == 0)
        n = 1;
    if (n > SYS_MAX_CPUS)
        n = SYS_MAX_CPUS;
    return n;
}

/* /sys/devices/system — one cpuN entry per logical CPU. */
int sys_devices_system_read_reg(char *buf, size_t count)
{
    uint32_t cpus;
    size_t off = 0;
    int n;

    if (!buf || count == 0)
        return -EINVAL;

    memset(buf, 0, count);
    cpus = sys_sysfs_cpu_count();
    for (uint32_t i = 0; i < cpus && off < count; i++)
    {
        n = snprintf(buf + off, (off < count) ? (count - off) : 0, "cpu%u\n", (unsigned)i);
        if (n <= 0 || n >= (int)(count - off))
            break;
        off += (size_t)n;
    }

    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

/* /sys/devices/system/cpuN — lists child attributes. */
int sys_devices_cpu_read_reg(char *buf, size_t count, unsigned cpu)
{
    size_t off = 0;
    int n;

    if (!buf || count == 0)
        return -EINVAL;
    if (cpu >= sys_sysfs_cpu_count())
        return -EINVAL;

    memset(buf, 0, count);
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "online\n");
    if (n > 0 && n < (int)(count - off))
        off += (size_t)n;
    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

int sys_devices_cpu_online_read_reg(char *buf, size_t count, unsigned cpu)
{
    int len;

    if (!buf || count == 0)
        return -EINVAL;
    if (cpu >= SYS_MAX_CPUS)
        return -EINVAL;

    memset(buf, 0, count);
    len = snprintf(buf, count, "%d\n", sys_devices_cpu_online[cpu]);
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    return len;
}

int sys_devices_cpu_online_write_reg(unsigned cpu, const char *buf, size_t count)
{
    char value_buf[32];
    size_t copy_len;

    if (!buf || count == 0)
        return 0;
    if (cpu >= SYS_MAX_CPUS)
        return -EINVAL;

    copy_len = (count < sizeof(value_buf) - 1) ? count : (sizeof(value_buf) - 1);
    memcpy(value_buf, buf, copy_len);
    value_buf[copy_len] = '\0';

    while (copy_len > 0 && (value_buf[copy_len - 1] == '\n' ||
                            value_buf[copy_len - 1] == '\r' ||
                            value_buf[copy_len - 1] == ' '))
    {
        copy_len--;
        value_buf[copy_len] = '\0';
    }

    if (copy_len == 1 && value_buf[0] == '0')
    {
        sys_devices_cpu_online[cpu] = 0;
        return (int)count;
    }
    if (copy_len == 1 && value_buf[0] == '1')
    {
        sys_devices_cpu_online[cpu] = 1;
        return (int)count;
    }

    return -EINVAL;
}

/*
 * Generate /sys/devices/block content.
 * Lists only present ATA disks (hda, hdb, hdc, hdd) and their partitions (hdX1, ...).
 */
int sys_devices_block_read_reg(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    memset(buf, 0, count);
    size_t off = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        const char *disk_name = block_dev_legacy_name(i);
        if (!disk_name || !block_dev_is_present(disk_name))
            continue;
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%s\n", disk_name);
        if (n <= 0 || n >= (int)(count - off))
            break;
        off += (size_t)n;
        int part_count = get_partition_count(i);
        for (int p = 0; p < part_count && off < count; p++)
        {
            partition_info_t pinfo;
            if (partition_nth_on_disk(i, (unsigned)p, &pinfo) != 0)
                continue;
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "hd%c%d\n", 'a' + (int)i, (int)pinfo.partition_number + 1);
            if (n <= 0 || n >= (int)(count - off))
                break;
            off += (size_t)n;
        }
    }
    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

/* Open /sys file */
int sysfs_open(const char *path, int flags)
{
    int pseudo_fd = -1;
    int64_t prc;

    (void)flags;  /* Most sysfs files support both read and write */

    if (!is_sys_path(path))
        return -EINVAL;

    pseudo_fs_nodes_register_all();
    prc = pseudo_fs_open_path(path, flags, &pseudo_fd);
    if (prc == 0)
    {
        proc_set_offset(pseudo_fd, 0);
        return pseudo_fd;
    }
    if (prc != -ENOENT)
        return (int)prc;

    return -ENOENT;
}

/* Read from /sys file */
int sysfs_read(int fd, char *buf, size_t count, off_t offset)
{
    int64_t pr;

    if (!buf || count == 0)
        return 0;

    if (pseudo_fs_find_by_fd(fd))
    {
        pr = pseudo_fs_read_fd(fd, buf, count, offset);
        return (int)pr;
    }

    return -EBADF;
}

/* Write to /sys file */
int sysfs_write(int fd, const char *buf, size_t count)
{
    int64_t pw;

    if (!buf || count == 0)
        return 0;

    if (pseudo_fs_find_by_fd(fd))
    {
        pw = pseudo_fs_write_fd(fd, buf, count);
        return (int)pw;
    }

    return -EBADF;
}

/* Get stat for /sys file */
int sysfs_stat(const char *path, stat_t *st)
{
    const pseudo_fs_entry_t *pf;

    if (!st || !is_sys_path(path))
        return -EINVAL;

    pseudo_fs_nodes_register_all();
    pf = pseudo_fs_lookup(path);
    if (pf && pf->ops && pf->ops->stat)
        return pf->ops->stat(pf->ctx, st);

    return -ENOENT;  /* File not found */
}

int sysfs_is_virtual_subdir(const char *path)
{
    (void)path;
    return 0;
}