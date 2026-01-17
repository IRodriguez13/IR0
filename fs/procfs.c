// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Simple /proc filesystem
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs.c
 * Description: Minimal /proc filesystem - on-demand, no mounting
 */

#include "procfs.h"
#include <ir0/stat.h>
#include <ir0/memory/kmem.h>
#include <ir0/memory/pmm.h>
#include <ir0/memory/allocator.h>
#include <ir0/vga.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <ir0/net.h>
#include <ir0/driver.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>
#include <ir0/version.h>
#include <drivers/serial/serial.h>
#include <drivers/timer/clock_system.h>
#include <arch/common/arch_portable.h>
#include <drivers/disk/partition.h>
#include <drivers/storage/ata.h>
#include <fs/vfs.h>

static pid_t proc_fd_pid_map[1000];
static int proc_fd_pid_map_init = 0;
/* Track offsets for /proc files */
static off_t proc_fd_offset_map[1000];
static int proc_fd_offset_map_init = 0;

static void proc_fd_pid_map_ensure_init(void)
{
    if (proc_fd_pid_map_init)
        return;
    for (int i = 0; i < (int)(sizeof(proc_fd_pid_map) / sizeof(proc_fd_pid_map[0])); i++)
        proc_fd_pid_map[i] = -1;
    proc_fd_pid_map_init = 1;
}

static void proc_fd_offset_map_ensure_init(void)
{
    if (proc_fd_offset_map_init)
        return;
    for (int i = 0; i < (int)(sizeof(proc_fd_offset_map) / sizeof(proc_fd_offset_map[0])); i++)
        proc_fd_offset_map[i] = 0;
    proc_fd_offset_map_init = 1;
}

static void proc_set_pid_for_fd(int fd, pid_t pid)
{
    proc_fd_pid_map_ensure_init();
    int idx = fd - 1000;
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_pid_map) / sizeof(proc_fd_pid_map[0])))
        proc_fd_pid_map[idx] = pid;
}

static pid_t proc_get_pid_for_fd(int fd)
{
    proc_fd_pid_map_ensure_init();
    int idx = fd - 1000;
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_pid_map) / sizeof(proc_fd_pid_map[0])))
        return proc_fd_pid_map[idx];
    return -1;
}
/**
 * get_memory_usage - Get total memory used by kernel
 *
 * Returns: Total memory used in bytes (physical frames + heap)
 */
static uint64_t get_memory_usage(void)
{
    size_t total_frames = 0;
    size_t used_frames = 0;
    size_t heap_total = 0;
    size_t heap_used = 0;
    uint64_t total_used = 0;
    
    /* Get physical memory statistics */
    pmm_stats(&total_frames, &used_frames, NULL);
    
    /* Get heap allocator statistics */
    alloc_stats(&heap_total, &heap_used, NULL);
    
    /* Calculate total used memory:
     * - Physical frames: used_frames * 4KB per frame
     * - Heap: heap_used bytes
     */
    total_used = ((uint64_t)used_frames * 4096) + (uint64_t)heap_used;
    
    return total_used;
}

/**
 * get_total_memory - Get total available memory
 *
 * Returns: Total memory available in bytes (physical frames + heap)
 */
static uint64_t get_total_memory(void)
{
    size_t total_frames = 0;
    size_t heap_total = 0;
    uint64_t total = 0;
    
    /* Get physical memory statistics */
    pmm_stats(&total_frames, NULL, NULL);
    
    /* Get heap allocator statistics */
    alloc_stats(&heap_total, NULL, NULL);
    
    /* Calculate total available memory:
     * - Physical frames: total_frames * 4KB per frame
     * - Heap: heap_total bytes
     */
    total = ((uint64_t)total_frames * 4096) + (uint64_t)heap_total;
    
    return total;
}

static int proc_ps_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;

    /* Initialize buffer to zero */
    memset(buf, 0, count);

    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "PID PPID STATE NAME\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;

    process_t *p = process_list;
    while (p && off < count - 1)
    {
        const char *state_str;
        switch (p->state)
        {
        case PROCESS_READY:
            state_str = "R";
            break;
        case PROCESS_RUNNING:
            state_str = "R";
            break;
        case PROCESS_BLOCKED:
            state_str = "S";
            break;
        case PROCESS_ZOMBIE:
            state_str = "Z";
            break;
        default:
            state_str = "?";
            break;
        }

        n = snprintf(buf + off, count - off,
                     "%d %d %s %s\n",
                     (int)p->task.pid,
                     (int)p->ppid,
                     state_str,
                     p->comm[0] ? p->comm : "(none)");
        if (n < 0)
            break;
        if (n >= (int)(count - off))
            n = (int)(count - off) - 1;
        off += (size_t)n;
        p = p->next;
    }

    /* Ensure null termination */
    if (off < count)
        buf[off] = '\0';

    return (int)off;
}

static int proc_netinfo_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;

    /* Initialize buffer to zero */
    memset(buf, 0, count);

    struct net_device *dev = net_get_devices();
    if (!dev)
    {
        int n = snprintf(buf, count, "No network devices\n");
        return (n < 0 || n >= (int)count) ? (int)count - 1 : n;
    }

    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "NAME MTU FLAGS MAC\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;

    while (dev && off < count - 1)
    {
        char flags[32];
        size_t foff = 0;
        flags[0] = '\0';

        if (dev->flags & IFF_UP)
            foff += (size_t)snprintf(flags + foff, (foff < sizeof(flags)) ? (sizeof(flags) - foff) : 0, "UP");
        if (dev->flags & IFF_RUNNING)
            foff += (size_t)snprintf(flags + foff, (foff < sizeof(flags)) ? (sizeof(flags) - foff) : 0, "%sRUNNING", (foff > 0) ? "," : "");
        if (dev->flags & IFF_BROADCAST)
            foff += (size_t)snprintf(flags + foff, (foff < sizeof(flags)) ? (sizeof(flags) - foff) : 0, "%sBROADCAST", (foff > 0) ? "," : "");
        if (foff == 0)
            snprintf(flags, sizeof(flags), "-");

        n = snprintf(buf + off, count - off,
                     "%s %u %s %02x:%02x:%02x:%02x:%02x:%02x\n",
                     dev->name ? dev->name : "",
                     (unsigned)dev->mtu,
                     flags,
                     (unsigned)dev->mac[0], (unsigned)dev->mac[1], (unsigned)dev->mac[2],
                     (unsigned)dev->mac[3], (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
        if (n < 0)
            break;
        if (n >= (int)(count - off))
            n = (int)(count - off) - 1;
        off += (size_t)n;
        dev = dev->next;
    }

    /* Ensure null termination */
    if (off < count)
        buf[off] = '\0';

    return (int)off;
}

static int proc_drivers_read(char *buf, size_t count)
{
    return ir0_driver_list_to_buffer(buf, count);
}

/* Check if path is in /proc */
bool is_proc_path(const char *path)
{
    return path && strncmp(path, "/proc/", 6) == 0;
}

/* Parse /proc path - returns entry name after /proc/, extracts PID if present */
static const char *proc_parse_path(const char *path, pid_t *pid_out)
{
    if (!is_proc_path(path))
        return NULL;
    
    /* Skip "/proc/" prefix */
    const char *after_proc = path + 6;
    
    /* Check if it's /proc/status (current process) */
    if (strncmp(after_proc, "status", 6) == 0)
    {
        *pid_out = -1;  /* Special value for current process */
        return "status";
    }
    
    /* Check if it's /proc/[pid]/status or /proc/[pid]/cmdline */
    char *slash = strchr(after_proc, '/');
    if (slash)
    {
        /* Extract PID from before the slash */
        char pid_str[16];
        size_t pid_len = (size_t)(slash - after_proc);
        if (pid_len < sizeof(pid_str) - 1)
        {
            strncpy(pid_str, after_proc, pid_len);
            pid_str[pid_len] = '\0';
            *pid_out = atoi(pid_str);
            
            /* Check what file it is */
            if (strncmp(slash + 1, "status", 6) == 0)
                return "status";
            else if (strncmp(slash + 1, "cmdline", 7) == 0)
                return "cmdline";
        }
    }
    
    /* Regular /proc files */
    *pid_out = -1;
    return after_proc;
}

/* Generate /proc/meminfo content */
int proc_meminfo_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    uint64_t total = get_total_memory();
    uint64_t used = get_memory_usage();
    uint64_t free = total - used;
    
    /* Convert to kilobytes */
    uint64_t total_kb = total / 1024;
    uint64_t free_kb = free / 1024;
    uint64_t used_kb = used / 1024;
    
    int len = snprintf(buf, count,
        "MemTotal: %llu kB\n"
        "MemFree:  %llu kB\n"
        "MemUsed:  %llu kB\n",
        (unsigned long long)total_kb,
        (unsigned long long)free_kb,
        (unsigned long long)used_kb
    );
    
    /* snprintf returns number of characters that would be written (excluding null terminator)
     * If it's >= count, it means the string was truncated */
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        /* Buffer was truncated, ensure null termination */
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    /* Ensure null termination */
    buf[len] = '\0';
    return len;
}

/* Generate /proc/[pid]/status content */
int proc_status_read(char *buf, size_t count, pid_t pid)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    process_t *proc = NULL;
    
    if (pid == -1)
    {
        /* /proc/status - current process */
        proc = current_process;
    } else
    {
        /* /proc/[pid]/status - specific process */
        proc = process_find_by_pid(pid);
    }
    
    if (!proc)
    {
        int len = snprintf(buf, count, "No such process\n");
        if (len < 0)
            return -1;
        if (len >= (int)count)
        {
            buf[count - 1] = '\0';
            return (int)(count - 1);
        }
        buf[len] = '\0';
        return len;
    }
    
    const char *state_str;
    switch (proc->state)
    {
        case PROCESS_READY:
            state_str = "R";
            break;
        case PROCESS_RUNNING:
            state_str = "R";
            break;
        case PROCESS_BLOCKED:
            state_str = "S";
            break;
        case PROCESS_ZOMBIE:
            state_str = "Z";
            break;
        default:
            state_str = "?";
            break;
    }
    
    int len = snprintf(buf, count,
        "Name: %s\n"
        "State: %s\n"
        "Pid: %d\n"
        "PPid: %d\n"
        "Uid: %d\n"
        "Gid: %d\n",
        proc->comm,
        state_str,
        proc->task.pid,
        proc->ppid,
        proc->uid,
        proc->gid
    );
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    buf[len] = '\0';
    return len;
}

/* Generate /proc/uptime content */
int proc_uptime_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    /* Convert to seconds */
    uint64_t uptime = get_system_time() / 1000;
    
    int len = snprintf(buf, count, "%llu.00\n", (unsigned long long)uptime);
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    buf[len] = '\0';
    return len;
}

/* Generate /proc/version content */
int proc_version_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    /* Use centralized version from version.h */
    /* Format matches Linux: "Linux version X.Y.Z (user@host) (compiler) (date)" */
    /* For IR0: "IR0 version X.Y.Z (built DATE TIME by user@host with compiler)" */
    int len = snprintf(buf, count,
        "IR0 version %s (built %s %s by %s@%s with %s)\n",
        IR0_VERSION_STRING,
        IR0_BUILD_DATE,
        IR0_BUILD_TIME,
        IR0_BUILD_USER,
        IR0_BUILD_HOST,
        IR0_BUILD_CC
    );
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    buf[len] = '\0';
    return len;
}

/* Generate /proc/cpuinfo content */
int proc_cpuinfo_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    uint32_t cpu_id = arch_get_cpu_id();
    uint32_t cpu_count = arch_get_cpu_count();
    const char *arch_name = arch_get_name();
    
    /* Get architecture bits */
    uint32_t arch_bits = 64;
#ifdef __x86_64__
    arch_bits = 64;
#elif defined(__i386__)
    arch_bits = 32;
#elif defined(__aarch64__)
    arch_bits = 64;
#elif defined(__arm__)
    arch_bits = 32;
#endif
    
    /* Get CPU vendor string */
    char vendor_str[13] = {0};
    extern int arch_get_cpu_vendor(char *vendor_buf);
    if (arch_get_cpu_vendor(vendor_str) < 0)
    {
        strncpy(vendor_str, "Unknown", sizeof(vendor_str) - 1);
    }
    
    /* Get CPU family, model, stepping */
    uint32_t family = 0;
    uint32_t model = 0;
    uint32_t stepping = 0;
    extern int arch_get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping);
    arch_get_cpu_signature(&family, &model, &stepping);
    
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
        "processor: %u\n"
        "vendor_id: %s\n"
        "cpu family: %u\n"
        "model: %u\n"
        "model name: %s\n"
        "stepping: %u\n",
        cpu_id,
        vendor_str,
        family,
        model,
        arch_name ? arch_name : "x86-64",
        stepping);
    
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    /* Add additional info */
    n = snprintf(buf + off, count - off,
        "cpu MHz: Unknown\n"
        "cache size: Unknown\n"
        "physical id: 0\n"
        "siblings: %u\n"
        "core id: 0\n"
        "cpu cores: 1\n"
        "apicid: %u\n"
        "initial apicid: %u\n"
        "fpu: yes\n"
        "fpu_exception: yes\n"
        "cpuid level: 1\n"
        "wp: yes\n"
        "flags: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2\n"
        "bogomips: Unknown\n"
        "clflush size: 64\n"
        "cache_alignment: 64\n"
        "address sizes: %ubits physical, %ubits virtual\n",
        cpu_count,
        cpu_id,
        cpu_id,
        arch_bits,
        arch_bits);
    
    if (n > 0 && n < (int)(count - off))
        off += (size_t)n;
    
    /* Ensure null termination */
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/loadavg content */
int proc_loadavg_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    /* Count running and ready processes */
    size_t running = 0;
    size_t ready = 0;
    process_t *p = process_list;
    
    while (p)
    {
        if (p->state == PROCESS_RUNNING)
            running++;
        else if (p->state == PROCESS_READY)
            ready++;
        p = p->next;
    }
    
    /* Simple load calculation: running processes + 0.5 * ready processes */
    /* Format: 1min 5min 15min running/total last_pid */
    int len = snprintf(buf, count,
        "%.2f %.2f %.2f %zu/%zu %d\n",
        (double)running + (double)ready * 0.5,
        (double)running + (double)ready * 0.4,
        (double)running + (double)ready * 0.3,
        running,
        running + ready,
        current_process ? (int)current_process->task.pid : 0
    );
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    buf[len] = '\0';
    return len;
}

/* Generate /proc/blockdevices content (lsblk-like output) */
int proc_blockdevices_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "NAME        MAJ:MIN   SIZE (bytes)    MODEL\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                 "------------------------------------------------\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    for (uint8_t i = 0; i < 4; i++)
    {
        if (!ata_drive_present(i))
            continue;
        
        uint64_t size = ata_get_size(i);
        const char *model = ata_get_model(i);
        const char *serial = ata_get_serial(i);
        
        /* Format: hda  MAJ:MIN   SIZE     MODEL (SERIAL) */
        char num_str[32];
        char *p = num_str;
        uint64_t tmp = size / (2 * 1024 * 1024); /* Convert to GB */
        if (tmp == 0) {
            *p++ = '0';
        } else {
            char rev[32];
            int idx = 0;
            while (tmp > 0) {
                rev[idx++] = '0' + (tmp % 10);
                tmp /= 10;
            }
            while (idx > 0)
                *p++ = rev[--idx];
        }
        *p = '\0';
        
        char name_buf[8];
        snprintf(name_buf, sizeof(name_buf), "hd%c", 'a' + i);
        
        n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "%-12s %3d:0   %5sG %s (%s)\n",
                     name_buf, i, num_str, model, serial);
        if (n < 0)
            return -1;
        if (n >= (int)(count - off))
            n = (int)(count - off) - 1;
        off += (size_t)n;
    }
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/filesystems content */
int proc_filesystems_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    size_t off = 0;
    /* Virtual filesystems (nodev) */
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "nodev proc\n"
                     "nodev devfs\n"
                     "nodev ramfs\n"
                     "nodev tmpfs\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    /* Physical filesystems (with devices) */
    n = snprintf(buf + off, count - off, "minix\n");
    if (n > 0 && n < (int)(count - off))
    {
        off += (size_t)n;
    }
    
    /* Ensure null termination */
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/partitions content */
int proc_partitions_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "major minor  #blocks  name\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    /* Iterate through all disks and their partitions */
    for (uint8_t disk_id = 0; disk_id < MAX_DISKS; disk_id++)
    {
        int part_count = get_partition_count(disk_id);
        if (part_count <= 0)
            continue;
        
        for (int part_num = 0; part_num < part_count; part_num++)
        {
            partition_info_t part_info;
            if (get_partition_info(disk_id, part_num, &part_info) != 0)
                continue;
            
            /* Format: major minor blocks name */
            char name_buf[16];
            snprintf(name_buf, sizeof(name_buf), "hd%c%d", 'a' + disk_id, part_num + 1);
            
            uint64_t blocks = part_info.total_sectors; /* sectors are 512-byte blocks */
            
            /* Convert blocks to string manually */
            char blocks_str[32];
            char *p = blocks_str;
            uint64_t tmp = blocks;
            if (tmp == 0)
            {
                *p++ = '0';
            }
            else
            {
                char rev[32];
                int idx = 0;
                while (tmp > 0)
                {
                    rev[idx++] = '0' + (tmp % 10);
                    tmp /= 10;
                }
                while (idx > 0)
                    *p++ = rev[--idx];
            }
            *p = '\0';
            
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%3d %6d %9s %s\n",
                         (int)disk_id, part_num + 1, blocks_str, name_buf);
            if (n < 0)
                break;
            if (n >= (int)(count - off))
                n = (int)(count - off) - 1;
            off += (size_t)n;
        }
    }
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/mounts content */
int proc_mounts_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    
    /* Try common mount points */
    const char *common_paths[] = { "/", "/tmp", "/proc", "/dev" };
    
    for (size_t i = 0; i < sizeof(common_paths) / sizeof(common_paths[0]); i++)
    {
        struct mount_point *mp = vfs_find_mount_point(common_paths[i]);
        if (mp)
        {
            int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                             "%s %s %s rw 0 0\n",
                             (mp->dev[0] != '\0') ? mp->dev : "none",
                             mp->path,
                             mp->fs_type ? mp->fs_type->name : "unknown");
            if (n > 0 && n < (int)(count - off))
                off += (size_t)n;
            else
                break;  /* Buffer full */
        }
        else if (strcmp(common_paths[i], "/proc") == 0)
        {
            /* /proc is virtual, always present */
            int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                             "none /proc proc rw 0 0\n");
            if (n > 0 && n < (int)(count - off))
                off += (size_t)n;
        }
        else if (strcmp(common_paths[i], "/dev") == 0)
        {
            /* /dev is virtual, always present */
            int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                             "none /dev devfs rw 0 0\n");
            if (n > 0 && n < (int)(count - off))
                off += (size_t)n;
        }
    }
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/interrupts content */
int proc_interrupts_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "           CPU0\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    /* IRQ mappings for x86 */
    const char *irq_names[] = {
        [0] = "  0:",
        [1] = "  1:",
        [2] = "  2:",
        [3] = "  3:",
        [4] = "  4:",
        [5] = "  5:",
        [6] = "  6:",
        [7] = "  7:",
        [8] = "  8:",
        [9] = "  9:",
        [10] = " 10:",
        [11] = " 11:",
        [12] = " 12:",
        [13] = " 13:",
        [14] = " 14:",
        [15] = " 15:",
    };
    
    const char *irq_descriptions[] = {
        [0] = "   timer",
        [1] = "   i8042",
        [5] = "   soundblaster",
        [11] = "   rtl8139",
        [12] = "   i8042",
        [14] = "   ata14",
        [15] = "   ata15",
    };
    
    for (int irq = 0; irq < 16; irq++)
    {
        if (irq_names[irq] && irq_descriptions[irq])
        {
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%s      %-20s (IRQ %d)\n",
                         irq_names[irq], irq_descriptions[irq], irq);
            if (n > 0 && n < (int)(count - off))
                off += (size_t)n;
        }
    }
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/iomem content */
int proc_iomem_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "00000000-0000FFFF : PCI Bus 0000:00\n"
                     "00000000-000003FF : PCI Bus 0000:00 - Reserved\n"
                     "00000400-000004FF : Reserved\n"
                     "00000500-000005FF : Reserved\n"
                     "00000600-000006FF : Reserved\n"
                     "00000700-000007FF : Reserved\n"
                     "00000A00-00000BFF : PCI Bus 0000:00 - Reserved\n"
                     "00000C00-00000DFF : PCI Bus 0000:00 - Reserved\n"
                     "00000E00-00000FFF : PCI Bus 0000:00 - Reserved\n"
                     "00001000-000010FF : PCI Bus 0000:00 - Reserved\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    /* Add common x86 I/O ranges */
    n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                 "00002000-000020FF : PIC (8259)\n"
                 "00002170-0000217F : ATA Secondary\n"
                 "00001F0-00001F7 : ATA Primary\n"
                 "0000220-000022F : Sound Blaster 16\n"
                 "000060-00006F : Keyboard/Mouse (PS/2)\n");
    if (n > 0 && n < (int)(count - off))
        off += (size_t)n;
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/ioports content */
int proc_ioports_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "0000-001f : dma1\n"
                     "0020-0021 : pic1\n"
                     "0040-0043 : timer0\n"
                     "0060-006f : keyboard\n"
                     "01f0-01f7 : ata primary\n"
                     "0170-0177 : ata secondary\n"
                     "0220-022f : sound blaster\n"
                     "0376-0376 : ata secondary control\n"
                     "03f6-03f6 : ata primary control\n");
    if (n < 0)
        return -1;
    if (n >= (int)(count - off))
        n = (int)(count - off) - 1;
    off += (size_t)n;
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/modules content (more detailed than /proc/drivers) */
int proc_modules_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    /* Use driver list function but format as modules output */
    /* Format: name size refcount dependencies */
    /* For now, use same content as /proc/drivers but with module format */
    int driver_size = ir0_driver_list_to_buffer(buf, count);
    if (driver_size <= 0)
        return driver_size;
    
    /* Convert driver list format to modules format */
    /* Driver format: "Driver Name Version\n" */
    /* Module format: "drivername size refcount dependencies\n" */
    char *pos = buf;
    char temp_buf[4096];
    size_t temp_off = 0;
    
    while (*pos && temp_off < sizeof(temp_buf) - 128)
    {
        char *line_start = pos;
        char *line_end = strchr(pos, '\n');
        if (!line_end)
            break;
        
        size_t line_len = (size_t)(line_end - line_start);
        if (line_len > 0 && line_len < 256)
        {
            char name[128] = {0};
            size_t name_len = line_len;
            if (name_len > sizeof(name) - 1)
                name_len = sizeof(name) - 1;
            
            /* Copy first word (driver name) */
            strncpy(name, line_start, name_len);
            name[name_len] = '\0';
            
            /* Extract just the name part (before spaces/tabs) */
            char *name_end = name;
            while (*name_end && *name_end != ' ' && *name_end != '\t' && *name_end != '\n')
                name_end++;
            *name_end = '\0';
            
            if (strlen(name) > 0)
            {
                int n = snprintf(temp_buf + temp_off, sizeof(temp_buf) - temp_off,
                                 "%-20s %8u %2d -\n",
                                 name,
                                 (unsigned int)256,  /* Estimated size */
                                 0);  /* Refcount */
                if (n > 0 && (size_t)n < sizeof(temp_buf) - temp_off)
                    temp_off += (size_t)n;
            }
        }
        pos = line_end + 1;
    }
    
    /* Copy formatted output back */
    if (temp_off > 0 && temp_off < count)
    {
        memcpy(buf, temp_buf, temp_off);
        buf[temp_off] = '\0';
        return (int)temp_off;
    }
    
    return (int)driver_size;
}

/* Generate /proc/timer_list content */
int proc_timer_list_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    memset(buf, 0, count);
    
    size_t off = 0;
    clock_stats_t stats;
    if (clock_get_stats(&stats) == 0)
    {
        const char *timer_name = "Unknown";
        switch (stats.active_timer)
        {
            case CLOCK_TIMER_NONE:
                timer_name = "None";
                break;
            case CLOCK_TIMER_PIT:
                timer_name = "PIT";
                break;
            case CLOCK_TIMER_HPET:
                timer_name = "HPET";
                break;
            case CLOCK_TIMER_LAPIC:
                timer_name = "LAPIC";
                break;
            case CLOCK_TIMER_RTC:
                timer_name = "RTC";
                break;
            default:
                timer_name = "Unknown";
                break;
        }
        
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "Timer: %s\n"
                         "Frequency: %u Hz\n"
                         "Tick Count: %llu\n"
                         "Uptime: %llu.%03u seconds\n",
                         timer_name,
                         stats.timer_frequency,
                         (unsigned long long)stats.tick_count,
                         (unsigned long long)stats.uptime_seconds,
                         stats.uptime_milliseconds);
        if (n > 0 && n < (int)(count - off))
            off += (size_t)n;
    }
    
    if (off < count)
        buf[off] = '\0';
    
    return (int)off;
}

/* Generate /proc/[pid]/cmdline content */
int proc_cmdline_read(char *buf, size_t count, pid_t pid)
{
    if (!buf || count == 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    process_t *proc = NULL;
    
    if (pid == -1)
    {
        /* Current process */
        proc = current_process;
    } else
    {
        /* Specific process */
        proc = process_find_by_pid(pid);
    }
    
    if (!proc)
    {
        /* No such process */
        return -1;
    }
    
    /* Get command name from process */
    int len = snprintf(buf, count, "%s", proc->comm[0] ? proc->comm : "(none)");
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    buf[len] = '\0';
    return len;
}

/* Get offset for /proc fd */
off_t proc_get_offset(int fd)
{
    proc_fd_offset_map_ensure_init();
    int idx = fd - 1000;
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_offset_map) / sizeof(proc_fd_offset_map[0])))
        return proc_fd_offset_map[idx];
    return 0;
}

/* Set offset for /proc fd */
void proc_set_offset(int fd, off_t offset)
{
    proc_fd_offset_map_ensure_init();
    int idx = fd - 1000;
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_offset_map) / sizeof(proc_fd_offset_map[0])))
        proc_fd_offset_map[idx] = offset;
}

/* Reset offset for /proc fd (when reopening) */
static void proc_reset_offset(int fd)
{
    proc_set_offset(fd, 0);
}

/* Open /proc file - returns special positive fd, stores PID in fd_table */
int proc_open(const char *path, int flags)
{
    /* Read-only for now */
    (void)flags;
    
    pid_t pid;
    const char *filename = proc_parse_path(path, &pid);
    if (!filename)
        return -1;
    
    int fd = -1;
    /* Check if file exists and return special positive fd */
    if (strcmp(filename, "meminfo") == 0)
    {
        fd = 1000;
    } else if (strcmp(filename, "ps") == 0) {
        fd = 1004;
    } else if (strcmp(filename, "netinfo") == 0) {
        fd = 1005;
    } else if (strcmp(filename, "drivers") == 0) {
        fd = 1006;
    } else if (strcmp(filename, "status") == 0)
    {
        proc_set_pid_for_fd(1001, pid);
        fd = 1001;
    } else if (strcmp(filename, "uptime") == 0)
    {
        fd = 1002;
    } else if (strcmp(filename, "version") == 0)
    {
        fd = 1003;
    } else if (strcmp(filename, "cpuinfo") == 0)
    {
        fd = 1007;
    } else if (strcmp(filename, "loadavg") == 0)
    {
        fd = 1008;
    } else if (strcmp(filename, "filesystems") == 0)
    {
        fd = 1009;
    } else if (strcmp(filename, "cmdline") == 0)
    {
        proc_set_pid_for_fd(1010, pid);
        fd = 1010;
    } else if (strcmp(filename, "blockdevices") == 0)
    {
        fd = 1011;
    } else if (strcmp(filename, "partitions") == 0)
    {
        fd = 1012;
    } else if (strcmp(filename, "mounts") == 0)
    {
        fd = 1013;
    } else if (strcmp(filename, "interrupts") == 0)
    {
        fd = 1014;
    } else if (strcmp(filename, "iomem") == 0)
    {
        fd = 1015;
    } else if (strcmp(filename, "ioports") == 0)
    {
        fd = 1016;
    } else if (strcmp(filename, "modules") == 0)
    {
        fd = 1017;
    } else if (strcmp(filename, "timer_list") == 0)
    {
        fd = 1018;
    } else
    {
        /* File not found */
        return -1;
    }
    
    /* Reset offset when opening */
    proc_reset_offset(fd);
    return fd;
}

/* Read from /proc file with offset support */
int proc_read(int fd, char *buf, size_t count, off_t offset)
{
    if (!buf || count == 0)
        return 0;
    
    /* Buffer to hold full content - initialize to zero to avoid garbage */
    static char proc_buffer[4096];
    memset(proc_buffer, 0, sizeof(proc_buffer));
    int full_size = 0;
    
    /* Generate full content based on fd */
    switch (fd)
    {
        case 1000:
            full_size = proc_meminfo_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1004:
            full_size = proc_ps_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1005:
            full_size = proc_netinfo_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1006:
            full_size = proc_drivers_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1001:
        {
            pid_t pid = proc_get_pid_for_fd(1001);
            full_size = proc_status_read(proc_buffer, sizeof(proc_buffer), pid);
            break;
        }
        case 1002:
            full_size = proc_uptime_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1003:
            full_size = proc_version_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1007:
            full_size = proc_cpuinfo_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1008:
            full_size = proc_loadavg_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1009:
            full_size = proc_filesystems_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1010:
        {
            pid_t pid = proc_get_pid_for_fd(1010);
            full_size = proc_cmdline_read(proc_buffer, sizeof(proc_buffer), pid);
            break;
        }
        case 1011:
            full_size = proc_blockdevices_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1012:
            full_size = proc_partitions_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1013:
            full_size = proc_mounts_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1014:
            full_size = proc_interrupts_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1015:
            full_size = proc_iomem_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1016:
            full_size = proc_ioports_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1017:
            full_size = proc_modules_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1018:
            full_size = proc_timer_list_read(proc_buffer, sizeof(proc_buffer));
            break;
        default:
            return -1;
    }
    
    if (full_size < 0)
        return -1;
    
    /* Ensure full_size doesn't exceed buffer size */
    if (full_size > (int)sizeof(proc_buffer))
        full_size = (int)sizeof(proc_buffer);
    
    /* Ensure null termination */
    if (full_size < (int)sizeof(proc_buffer))
        proc_buffer[full_size] = '\0';
    
    /* If offset is beyond file size, return 0 (EOF) */
    if (offset < 0 || offset >= (off_t)full_size)
        return 0;
    
    /* Calculate how many bytes we can read */
    size_t remaining = (size_t)full_size - (size_t)offset;
    size_t to_read = (count < remaining) ? count : remaining;
    
    /* Safety check: ensure we don't exceed buffer bounds */
    if ((size_t)offset + to_read > sizeof(proc_buffer))
        to_read = sizeof(proc_buffer) - (size_t)offset;
    
    if (to_read == 0)
        return 0;
    
    /* Copy the appropriate portion using memcpy for efficiency and safety */
    memcpy(buf, proc_buffer + (size_t)offset, to_read);
    
    return (int)to_read;
}

/* Write to /proc file (not implemented) */
int proc_write(int fd, const char *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    /* Read-only for now */
    return -1;
}

/* Get stat for /proc file */
int proc_stat(const char *path, stat_t *st)
{
    if (!st || !is_proc_path(path))
        return -1;
    
    pid_t pid;
    const char *filename = proc_parse_path(path, &pid);
    if (!filename)
        return -1;
    
    /* Check if file exists */
    if (strcmp(filename, "meminfo") == 0 ||
        strcmp(filename, "ps") == 0 ||
        strcmp(filename, "netinfo") == 0 ||
        strcmp(filename, "drivers") == 0 ||
        strcmp(filename, "status") == 0 ||
        strcmp(filename, "uptime") == 0 ||
        strcmp(filename, "version") == 0 ||
        strcmp(filename, "cpuinfo") == 0 ||
        strcmp(filename, "loadavg") == 0 ||
        strcmp(filename, "filesystems") == 0 ||
        strcmp(filename, "cmdline") == 0 ||
        strcmp(filename, "blockdevices") == 0) {
        
        memset(st, 0, sizeof(stat_t));
        /* Regular file, read-only */
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        /* root user */
        st->st_uid = 0;
        /* root group */
        st->st_gid = 0;
        /* Approximate size */
        st->st_size = 1024;
        
        return 0;
    }
    
    /* File not found */
    return -1;
}
