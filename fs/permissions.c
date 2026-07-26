/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: permissions.c
 * Description: IR0 kernel source/header file
 */

#include "permissions.h"
#include <ir0/credentials.h>
#include <string.h>

/*
 * Accounts and passwords are userspace policy (/etc/passwd, /etc/shadow,
 * /etc/group). The kernel only provides the mechanism: credentials on the
 * task, set-id transitions on exec and these permission checks.
 */

/* Get current process UID */
uint32_t get_current_uid(void)
{
    const struct ir0_task_cred *c;

    c = ir0_current_cred();
    return c ? (uint32_t)c->euid : ROOT_UID;
}

/* Get current process GID */
uint32_t get_current_gid(void)
{
    const struct ir0_task_cred *c;

    c = ir0_current_cred();
    return c ? (uint32_t)c->egid : ROOT_GID;
}

bool ir0_access_from_stat(const stat_t *st, int mode, uid_t euid, gid_t egid)
{
    uint16_t file_mode;
    uint32_t file_uid;
    uint32_t file_gid;

    if (!st)
        return false;

    if (euid == ROOT_UID)
        return true;

    file_mode = st->st_mode;
    file_uid = st->st_uid;
    file_gid = st->st_gid;

    if (euid == file_uid)
    {
        if ((mode & ACCESS_READ) && !(file_mode & S_IRUSR))
            return false;
        if ((mode & ACCESS_WRITE) && !(file_mode & S_IWUSR))
            return false;
        if ((mode & ACCESS_EXEC) && !(file_mode & S_IXUSR))
            return false;
        return true;
    }

    if (egid == file_gid)
    {
        if ((mode & ACCESS_READ) && !(file_mode & S_IRGRP))
            return false;
        if ((mode & ACCESS_WRITE) && !(file_mode & S_IWGRP))
            return false;
        if ((mode & ACCESS_EXEC) && !(file_mode & S_IXGRP))
            return false;
        return true;
    }

    if ((mode & ACCESS_READ) && !(file_mode & S_IROTH))
        return false;
    if ((mode & ACCESS_WRITE) && !(file_mode & S_IWOTH))
        return false;
    if ((mode & ACCESS_EXEC) && !(file_mode & S_IXOTH))
        return false;

    return true;
}

bool ir0_access_from_stat_groups(const stat_t *st, int mode, uid_t euid,
                                 gid_t egid, const gid_t *groups, int ngroups)
{
    size_t i;

    if (ir0_access_from_stat(st, mode, euid, egid))
        return true;

    if (!groups || ngroups <= 0)
        return false;

    for (i = 0; i < (size_t)ngroups; i++)
    {
        if (groups[i] == egid)
            continue;
        if (ir0_access_from_stat(st, mode, euid, groups[i]))
            return true;
    }

    return false;
}