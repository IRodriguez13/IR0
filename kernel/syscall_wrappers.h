/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - System Call Wrappers
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for safe syscall wrappers
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

typedef uint32_t mode_t;

/* ========================================================================== */
/* SAFE WRAPPERS                                                              */
/* ========================================================================== */

int64_t safe_sys_write(int fd, const void *buf, size_t count);
int64_t safe_sys_read(int fd, void *buf, size_t count);
int64_t safe_sys_open(const char *pathname, int flags, mode_t mode);
int64_t safe_sys_mkdir(const char *pathname, mode_t mode);
void *safe_kmalloc(size_t size);
