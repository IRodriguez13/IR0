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

/* /sys filesystem interface */
bool is_sys_path(const char *path);
int sysfs_open(const char *path, int flags);
int sysfs_read(int fd, char *buf, size_t count, off_t offset);
int sysfs_write(int fd, const char *buf, size_t count);
int sysfs_stat(const char *path, stat_t *st);

