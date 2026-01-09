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
#include <kernel/process.h>
#include <kernel/syscalls.h>
#include <config.h>
#include <drivers/serial/serial.h>
#include <drivers/timer/clock_system.h>

/* External functions */
extern uint64_t get_system_time(void);

/* Simple implementation of get_process_fd_table for procfs use */
static fd_entry_t *get_process_fd_table(void)
{
    extern process_t *current_process;
    if (!current_process)
        return NULL;
    return current_process->fd_table;
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
    if (strcmp(filename, "status") == 0) {
        // Store PID in fd_table for later use
        fd_entry_t *fd_table = get_process_fd_table();
        if (fd_table) {
            fd_table[1001].offset = pid;  // Abuse offset field to store PID
        }
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
        case 1001: {
            // Get stored PID from fd_table
            fd_entry_t *fd_table = get_process_fd_table();
            pid_t pid = fd_table ? fd_table[1001].offset : -1;
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
