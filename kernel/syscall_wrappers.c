// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_wrappers.c
 * Description: Wrapper functions for system calls to provide safer interfaces
 */

#include "syscalls.h"
#include "process.h"
#include <ir0/memory/kmem.h>
#include <string.h>

/**
 * Safe wrapper for sys_write that validates parameters
 */
int64_t safe_sys_write(int fd, const void *buf, size_t count) {
    if (!buf || count == 0) {
        return -1;
    }
    
    if (fd < 0 || fd >= MAX_FDS) {
        return -1;
    }
    
    return sys_write(fd, buf, count);
}

/**
 * Safe wrapper for sys_read that validates parameters
 */
int64_t safe_sys_read(int fd, void *buf, size_t count) {
    if (!buf || count == 0) {
        return -1;
    }
    
    if (fd < 0 || fd >= MAX_FDS) {
        return -1;
    }
    
    return sys_read(fd, buf, count);
}

/**
 * Safe wrapper for sys_open that validates pathname
 */
int64_t safe_sys_open(const char *pathname, int flags, mode_t mode) {
    if (!pathname || strlen(pathname) == 0) {
        return -1;
    }
    
    if (strlen(pathname) > 255) {
        return -1;
    }
    
    return sys_open(pathname, flags, mode);
}

/**
 * Safe wrapper for sys_mkdir that validates pathname and mode
 */
int64_t safe_sys_mkdir(const char *pathname, mode_t mode) {
    if (!pathname || strlen(pathname) == 0) {
        return -1;
    }
    
    if (strlen(pathname) > 255) {
        return -1;
    }
    
    /* Ensure reasonable permissions */
    mode &= 0777;
    
    return sys_mkdir(pathname, mode);
}

/**
 * Wrapper for process creation with validation
 */
process_t *safe_process_create(const char *name, void *entry_point) {
    if (!name || !entry_point) {
        return NULL;
    }
    
    if (strlen(name) == 0 || strlen(name) > 63) {
        return NULL;
    }
    
    return process_create(name, entry_point);
}

/**
 * Safe memory allocation wrapper
 */
void *safe_kmalloc(size_t size) {
    if (size == 0 || size > (1024 * 1024)) { /* Max 1MB allocation */
        return NULL;
    }
    
    return kmalloc(size);
}