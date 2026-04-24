/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: permissions.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <ir0/types.h>

/* Forward declaration */
struct process;

/* Simple user system - hardcoded */
#define ROOT_UID     0
#define ROOT_GID     0
#define USER_UID     1000
#define USER_GID     1000

/* Default umask (022 = ----w--w-) */
#define DEFAULT_UMASK 0022

/* Access modes for check_file_access() */
#define ACCESS_READ    1
#define ACCESS_WRITE   2
#define ACCESS_EXEC    4

/* Function declarations */
bool check_file_access(const char *path, int mode, const struct process *process);
uint32_t get_current_uid(void);
uint32_t get_current_gid(void);
bool is_root(const struct process *process);
void init_simple_users(void);
