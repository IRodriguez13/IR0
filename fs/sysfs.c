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
#include <ir0/driver.h>
#include <ir0/version.h>
#include <string.h>
#include <errno.h>

/* Forward declarations for functions we use */
extern bool ata_drive_present(uint8_t drive);

#define SYS_BUFFER_SIZE 4096
#define SYS_FD_BASE 3000  /* sysfs uses FD range 3000-3999 */
#define SYS_DEFAULT_FILE_SIZE 256

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

/* Parse /sys path - returns entry path after /sys/ */
static const char *sys_parse_path(const char *path)
{
    if (!is_sys_path(path))
        return NULL;
    
    /* Skip "/sys/" prefix */
    return path + 5;
}

/**
 * sys_kernel_version_read - Read kernel version from /sys/kernel/version
 * @buf: Buffer to write version string
 * @count: Size of buffer
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
static int sys_kernel_version_read(char *buf, size_t count)
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
 * sys_kernel_hostname_read - Read system hostname from /sys/kernel/hostname
 * @buf: Buffer to write hostname
 * @count: Size of buffer
 * 
 * Returns: Number of bytes written on success, negative error on failure
 */
static int sys_kernel_hostname_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    /* Return kernel hostname (could be configurable) */
    int len = snprintf(buf, count, "ir0-kernel\n");
    
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
static int sys_kernel_max_processes_read(char *buf, size_t count)
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
static int sys_kernel_max_processes_write(const char *buf, size_t count)
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

/* Generate /sys/devices/system content */
static int sys_devices_system_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "cpu0\n");
    if (n > 0 && n < (int)(count - off))
        off += (size_t)n;
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /sys/devices/system/cpu0 content */
static int sys_devices_cpu0_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    /* List CPU properties */
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "online\n");
    if (n > 0 && n < (int)(count - off))
        off += (size_t)n;
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /sys/devices/system/cpu0/online content (readable/writable) */
static int sys_devices_cpu0_online = 1;  /* CPU is online by default */

static int sys_devices_cpu0_online_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    int len = snprintf(buf, count, "%d\n", sys_devices_cpu0_online);
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    return len;
}

static int sys_devices_cpu0_online_write(const char *buf, size_t count)
{
    if (!buf || count == 0)
        return 0;
    
    /* Parse 0 or 1 */
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
    
    if (copy_len == 1 && value_buf[0] == '0')
    {
        sys_devices_cpu0_online = 0;
        return (int)count;
    }
    else if (copy_len == 1 && value_buf[0] == '1')
    {
        sys_devices_cpu0_online = 1;
        return (int)count;
    }
    
    return -EINVAL;
}

/* Generate /sys/devices/block content */
static int sys_devices_block_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -EINVAL;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    
    /* List block devices (ATA drives) */
    for (uint8_t i = 0; i < 4; i++)
    {
        if (!ata_drive_present(i))
            continue;
        
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "hda%c\n", 'a' + i);
        if (n > 0 && n < (int)(count - off))
            off += (size_t)n;
        else
            break;
    }
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Open /sys file */
int sysfs_open(const char *path, int flags)
{
    (void)flags;  /* Most sysfs files support both read and write */
    
    if (!is_sys_path(path))
        return -1;
    
    const char *sys_path = sys_parse_path(path);
    if (!sys_path)
        return -1;
    
    int fd = -1;
    
    /* Map paths to FD numbers */
    if (strcmp(sys_path, "kernel/version") == 0)
    {
        fd = SYS_FD_BASE + 0;
    }
    else if (strcmp(sys_path, "kernel/hostname") == 0)
    {
        fd = SYS_FD_BASE + 1;
    }
    else if (strcmp(sys_path, "kernel/max_processes") == 0)
    {
        fd = SYS_FD_BASE + 2;
    }
    else if (strcmp(sys_path, "devices/system") == 0)
    {
        fd = SYS_FD_BASE + 10;
    }
    else if (strcmp(sys_path, "devices/system/cpu0") == 0)
    {
        fd = SYS_FD_BASE + 11;
    }
    else if (strcmp(sys_path, "devices/system/cpu0/online") == 0)
    {
        fd = SYS_FD_BASE + 12;
    }
    else if (strcmp(sys_path, "devices/block") == 0)
    {
        fd = SYS_FD_BASE + 20;
    }
    else
    {
        return -1;  /* File not found */
    }
    
    /* Reset offset when opening (reuse procfs offset tracking) */
    proc_set_offset(fd, 0);
    
    return fd;
}

/* Read from /sys file */
int sysfs_read(int fd, char *buf, size_t count, off_t offset)
{
    if (!buf || count == 0)
        return 0;
    
    if (fd < SYS_FD_BASE || fd >= SYS_FD_BASE + 1000)
        return -1;
    
    static char sys_buffer[SYS_BUFFER_SIZE];
    memset(sys_buffer, 0, sizeof(sys_buffer));
    int full_size = 0;
    
    /* Generate full content based on fd */
    int sys_fd = fd - SYS_FD_BASE;
    
    switch (sys_fd)
    {
        case 0:
            full_size = sys_kernel_version_read(sys_buffer, sizeof(sys_buffer));
            break;
        case 1:
            full_size = sys_kernel_hostname_read(sys_buffer, sizeof(sys_buffer));
            break;
        case 2:
            full_size = sys_kernel_max_processes_read(sys_buffer, sizeof(sys_buffer));
            break;
        case 10:
            full_size = sys_devices_system_read(sys_buffer, sizeof(sys_buffer));
            break;
        case 11:
            full_size = sys_devices_cpu0_read(sys_buffer, sizeof(sys_buffer));
            break;
        case 12:
            full_size = sys_devices_cpu0_online_read(sys_buffer, sizeof(sys_buffer));
            break;
        case 20:
            full_size = sys_devices_block_read(sys_buffer, sizeof(sys_buffer));
            break;
        default:
            return -1;
    }
    
    if (full_size < 0)
        return -1;
    
    if (full_size > (int)sizeof(sys_buffer))
        full_size = (int)sizeof(sys_buffer);
    
    if (full_size < (int)sizeof(sys_buffer))
        sys_buffer[full_size] = '\0';
    
    /* If offset is beyond file size, return 0 (EOF) */
    if (offset < 0 || offset >= (off_t)full_size)
        return 0;
    
    size_t remaining = (size_t)full_size - (size_t)offset;
    size_t to_read = (count < remaining) ? count : remaining;
    
    if ((size_t)offset + to_read > sizeof(sys_buffer))
        to_read = sizeof(sys_buffer) - (size_t)offset;
    
    if (to_read == 0)
        return 0;
    
    memcpy(buf, sys_buffer + (size_t)offset, to_read);
    
    return (int)to_read;
}

/* Write to /sys file */
int sysfs_write(int fd, const char *buf, size_t count)
{
    if (!buf || count == 0)
        return 0;
    
    if (fd < SYS_FD_BASE || fd >= SYS_FD_BASE + 1000)
        return -1;
    
    int sys_fd = fd - SYS_FD_BASE;
    
    switch (sys_fd)
    {
        case 2:  /* /sys/kernel/max_processes */
            return sys_kernel_max_processes_write(buf, count);
        case 12:  /* /sys/devices/system/cpu0/online */
            return sys_devices_cpu0_online_write(buf, count);
        default:
            return -EACCES;  /* Read-only file */
    }
}

/* Get stat for /sys file */
int sysfs_stat(const char *path, stat_t *st)
{
    if (!st || !is_sys_path(path))
        return -1;
    
    const char *sys_path = sys_parse_path(path);
    if (!sys_path)
        return -1;
    
    /* Check if file exists */
    if (strcmp(sys_path, "kernel/version") == 0 ||
        strcmp(sys_path, "kernel/hostname") == 0 ||
        strcmp(sys_path, "kernel/max_processes") == 0 ||
        strcmp(sys_path, "devices/system") == 0 ||
        strcmp(sys_path, "devices/system/cpu0") == 0 ||
        strcmp(sys_path, "devices/system/cpu0/online") == 0 ||
        strcmp(sys_path, "devices/block") == 0)
    {
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFREG | 0644;  /* Regular file, readable by all */
        st->st_nlink = 1;
        st->st_uid = 0;
        st->st_gid = 0;
        st->st_size = SYS_DEFAULT_FILE_SIZE;
        
        /* Writable files have different permissions */
        if (strcmp(sys_path, "kernel/max_processes") == 0 ||
            strcmp(sys_path, "devices/system/cpu0/online") == 0)
        {
            st->st_mode = S_IFREG | 0664;  /* Writable by owner/group */
        }
        
        return 0;
    }
    
    return -1;  /* File not found */
}