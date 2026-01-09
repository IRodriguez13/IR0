// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Simple /proc filesystem
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs.h
 * Description: Minimal /proc filesystem - on-demand, no mounting
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <kernel/process.h>

/* /proc entry types */
typedef enum {
    PROC_TYPE_FILE,
    PROC_TYPE_DIR
} proc_type_t;

/* /proc entry structure */
typedef struct proc_entry {
    char name[64];
    proc_type_t type;
    int (*read_func)(char *buf, size_t count);
    int (*write_func)(const char *buf, size_t count);
    struct proc_entry *children;
    int child_count;
} proc_entry_t;

/* /proc filesystem interface */
bool is_proc_path(const char *path);
int proc_open(const char *path, int flags);
int proc_read(int fd, char *buf, size_t count);
int proc_write(int fd, const char *buf, size_t count);
int proc_stat(const char *path, stat_t *st);

/* /proc entry generators */
int proc_meminfo_read(char *buf, size_t count);
int proc_status_read(char *buf, size_t count, pid_t pid);
int proc_uptime_read(char *buf, size_t count);
int proc_version_read(char *buf, size_t count);
