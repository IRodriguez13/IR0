/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sysfs.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - SYSFS (System Filesystem) Header
 * Copyright (C) 2025  Iván Rodriguez
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>

/* /sys filesystem interface
 *
 * Legacy note: dispatch matches procfs — FD-based open/read/write in fs/sysfs.c.
 * Prefer devfs_ops_t-style per-node registration for new sysfs endpoints.
 */
bool is_sys_path(const char *path);
int sysfs_open(const char *path, int flags);
int sysfs_read(int fd, char *buf, size_t count, off_t offset);
int sysfs_write(int fd, const char *buf, size_t count);
int sysfs_stat(const char *path, stat_t *st);

/* Shared implementations registered via pseudo_fs_nodes.c */
int sys_kernel_hostname_read_reg(char *buf, size_t count);
int sys_kernel_hostname_write_reg(const char *buf, size_t count);
int sys_kernel_version_read_reg(char *buf, size_t count);
int sys_kernel_max_processes_read_reg(char *buf, size_t count);
int sys_kernel_max_processes_write_reg(const char *buf, size_t count);
int sys_console_mode_read_reg(char *buf, size_t count);
int sys_devices_system_read_reg(char *buf, size_t count);
int sys_devices_cpu_read_reg(char *buf, size_t count, unsigned cpu);
int sys_devices_cpu_online_read_reg(char *buf, size_t count, unsigned cpu);
int sys_devices_cpu_online_write_reg(unsigned cpu, const char *buf, size_t count);
int sys_devices_block_read_reg(char *buf, size_t count);


