/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: credentials.h
 * Description: Facade for current-task credentials (identity, umask, cwd).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <ir0/types.h>

struct ir0_task_cred
{
    uid_t uid;
    gid_t gid;
    uid_t euid;
    gid_t egid;
    mode_t umask;
    pid_t pid;

    /*
     * Points at the current process cwd buffer (nul-terminated) while the
     * caller holds the scheduler lock / runs in syscall context.
     */
    const char *cwd;
};

const struct ir0_task_cred *ir0_current_cred(void);
pid_t ir0_current_pid(void);
mode_t ir0_current_umask(void);
int ir0_cred_is_root(void);

bool ir0_check_file_access(const char *path, int mode);
