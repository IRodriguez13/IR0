/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: validate_user.h
 * Description: userspace pointer validation helpers (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>

int validate_userspace_string(const char *str, size_t max_len);
int validate_userspace_buffer(const void *buf, size_t size);
