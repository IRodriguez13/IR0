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
#include <ir0/vga.h>
#include <string.h>
#include <stdlib.h>
#include <ir0/net.h>
#include <ir0/driver.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>
#include <config.h>
#include <drivers/serial/serial.h>
#include <drivers/timer/clock_system.h>

/* External functions */
extern uint64_t get_system_time(void);

static pid_t proc_fd_pid_map[1000];
static int proc_fd_pid_map_init = 0;

static void proc_fd_pid_map_ensure_init(void)
{
    if (proc_fd_pid_map_init)
        return;
    for (int i = 0; i < (int)(sizeof(proc_fd_pid_map) / sizeof(proc_fd_pid_map[0])); i++)
        proc_fd_pid_map[i] = -1;
    proc_fd_pid_map_init = 1;
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
static uint64_t get_memory_usage(void)
{
    // TODO: Implement proper memory tracking
    return 8 * 1024 * 1024;  // 8MB used for now
}

static uint64_t get_total_memory(void)
{
    // TODO: Get from memory manager
    return 16 * 1024 * 1024;  // 16MB total for now
}

static int proc_ps_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;

    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "PID\tPPID\tSTATE\tNAME\n");
    if (n < 0)
        return -1;
    off += (size_t)n;

    process_t *p = process_list;
    while (p && off < count)
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
                     "%d\t%d\t%s\t%s\n",
                     (int)p->task.pid,
                     (int)p->ppid,
                     state_str,
                     p->comm[0] ? p->comm : "(none)");
        if (n < 0)
            break;
        off += (size_t)n;
        p = p->next;
    }

    return (off < count) ? (int)off : (int)count;
}

static int proc_netinfo_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;

    struct net_device *dev = net_get_devices();
    if (!dev)
        return snprintf(buf, count, "No network devices\n");

    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                     "NAME\tMTU\tFLAGS\tMAC\n");
    if (n < 0)
        return -1;
    off += (size_t)n;

    while (dev && off < count)
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
                     "%s\t%u\t%s\t%02x:%02x:%02x:%02x:%02x:%02x\n",
                     dev->name ? dev->name : "",
                     (unsigned)dev->mtu,
                     flags,
                     (unsigned)dev->mac[0], (unsigned)dev->mac[1], (unsigned)dev->mac[2],
                     (unsigned)dev->mac[3], (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
        if (n < 0)
            break;
        off += (size_t)n;
        dev = dev->next;
    }

    return (off < count) ? (int)off : (int)count;
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
    
    const char *after_proc = path + 6;  // Skip "/proc/"
    
    // Check if it's /proc/[pid]/status
    if (strncmp(after_proc, "status", 6) == 0) {
        // It's /proc/status (current process)
        *pid_out = -1;  // Special value for current process
        return "status";
    }
    
    // Check if it's /proc/[pid]/status
    char *slash = strchr(after_proc, '/');
    if (slash && strncmp(slash + 1, "status", 6) == 0) {
        // Extract PID from before the slash
        char pid_str[16];
        int pid_len = slash - after_proc;
        if (pid_len < sizeof(pid_str) - 1) {
            strncpy(pid_str, after_proc, pid_len);
            pid_str[pid_len] = '\0';
            *pid_out = atoi(pid_str);
            return "status";
        }
    }
    
    // Regular /proc files
    *pid_out = -1;
    return after_proc;
}

/* Generate /proc/meminfo content */
int proc_meminfo_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    uint64_t total = get_total_memory();
    uint64_t used = get_memory_usage();
    uint64_t free = total - used;
    
    int len = snprintf(buf, count,
        "MemTotal: %lu kB\n"
        "MemFree:  %lu kB\n"
        "MemUsed:  %lu kB\n",
        total / 1024,
        free / 1024,
        used / 1024
    );
    
    return (len < count) ? len : count;
}

/* Generate /proc/[pid]/status content */
int proc_status_read(char *buf, size_t count, pid_t pid)
{
    if (!buf || count == 0)
        return -1;
    
    process_t *proc = NULL;
    
    if (pid == -1) {
        // /proc/status - current process
        proc = current_process;
    } else {
        // /proc/[pid]/status - specific process
        proc = process_find_by_pid(pid);
    }
    
    if (!proc)
        return snprintf(buf, count, "No such process\n");
    
    const char *state_str;
    switch (proc->state) {
        case PROCESS_READY: state_str = "R"; break;
        case PROCESS_RUNNING: state_str = "R"; break;
        case PROCESS_BLOCKED: state_str = "S"; break;
        case PROCESS_ZOMBIE: state_str = "Z"; break;
        default: state_str = "?"; break;
    }
    
    int len = snprintf(buf, count,
        "Name:\t%s\n"
        "State:\t%s\n"
        "Pid:\t%d\n"
        "PPid:\t%d\n"
        "Uid:\t%d\n"
        "Gid:\t%d\n",
        proc->comm,
        state_str,
        proc->task.pid,
        proc->ppid,
        proc->uid,
        proc->gid
    );
    
    return (len < count) ? len : count;
}

/* Generate /proc/uptime content */
int proc_uptime_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    uint64_t uptime = get_system_time() / 1000;  // Convert to seconds
    
    int len = snprintf(buf, count, "%lu.00\n", uptime);
    return (len < count) ? len : count;
}

/* Generate /proc/version content */
int proc_version_read(char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    int len = snprintf(buf, count,
        "IR0 version %s (built %s %s)\n",
        IR0_VERSION_STRING,
        __DATE__,
        __TIME__
    );
    
    return (len < count) ? len : count;
}

/* Handle /proc file read */
static int proc_file_read(const char *filename, char *buf, size_t count)
{
    if (strcmp(filename, "meminfo") == 0) {
        return proc_meminfo_read(buf, count);
    } else if (strcmp(filename, "ps") == 0) {
        return proc_ps_read(buf, count);
    } else if (strcmp(filename, "netinfo") == 0) {
        return proc_netinfo_read(buf, count);
    } else if (strcmp(filename, "drivers") == 0) {
        return proc_drivers_read(buf, count);
    } else if (strcmp(filename, "status") == 0) {
        return proc_status_read(buf, count, -1);  // Current process
    } else if (strcmp(filename, "uptime") == 0) {
        return proc_uptime_read(buf, count);
    } else if (strcmp(filename, "version") == 0) {
        return proc_version_read(buf, count);
    }
    
    return -1;  // File not found
}

/* Open /proc file - returns special positive fd, stores PID in fd_table */
int proc_open(const char *path, int flags)
{
    (void)flags;  // Read-only for now
    
    pid_t pid;
    const char *filename = proc_parse_path(path, &pid);
    if (!filename)
        return -1;
    
    // Check if file exists and return special positive fd
    if (strcmp(filename, "meminfo") == 0) return 1000;
    if (strcmp(filename, "ps") == 0) return 1004;
    if (strcmp(filename, "netinfo") == 0) return 1005;
    if (strcmp(filename, "drivers") == 0) return 1006;
    if (strcmp(filename, "status") == 0) {
        proc_set_pid_for_fd(1001, pid);
        return 1001;
    }
    if (strcmp(filename, "uptime") == 0) return 1002;
    if (strcmp(filename, "version") == 0) return 1003;
    
    return -1;  // File not found
}

/* Read from /proc file */
int proc_read(int fd, char *buf, size_t count)
{
    if (!buf || count == 0)
        return -1;
    
    switch (fd) {
        case 1000: return proc_meminfo_read(buf, count);
        case 1004: return proc_ps_read(buf, count);
        case 1005: return proc_netinfo_read(buf, count);
        case 1006: return proc_drivers_read(buf, count);
        case 1001: {
            pid_t pid = proc_get_pid_for_fd(1001);
            return proc_status_read(buf, count, pid);
        }
        case 1002: return proc_uptime_read(buf, count);
        case 1003: return proc_version_read(buf, count);
        default: return -1;
    }
}

/* Write to /proc file (not implemented) */
int proc_write(int fd, const char *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return -1;  // Read-only for now
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
    
    // Check if file exists
    if (strcmp(filename, "meminfo") == 0 ||
        strcmp(filename, "ps") == 0 ||
        strcmp(filename, "netinfo") == 0 ||
        strcmp(filename, "drivers") == 0 ||
        strcmp(filename, "status") == 0 ||
        strcmp(filename, "uptime") == 0 ||
        strcmp(filename, "version") == 0) {
        
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFREG | 0444;  // Regular file, read-only
        st->st_nlink = 1;
        st->st_uid = 0;  // root
        st->st_gid = 0;  // root
        st->st_size = 1024;  // Approximate size
        
        return 0;
    }
    
    return -1;  // File not found
}
