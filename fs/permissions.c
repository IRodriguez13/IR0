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
 * Description: DAC permission check (Linux fs/namei.c generic_permission()).
 */

#include <ir0/cred.h>
#include <string.h>

/*
 * Accounts and passwords are userspace policy (/etc/passwd, /etc/shadow,
 * /etc/group). The kernel only provides the mechanism: credentials on the
 * task, set-id transitions on exec and these permission checks.
 */

static bool dac_class_allows(const stat_t *st, int mode, uid_t euid,
                             gid_t egid)
{
    uint16_t file_mode;
    uint32_t file_uid;
    uint32_t file_gid;

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

int generic_permission(const struct ir0_task_cred *cred, const stat_t *st,
                       int mask)
{
    int i;

    if (!cred || !st)
        return -EACCES;

    if (dac_class_allows(st, mask, cred->euid, cred->egid))
        return 0;

    if (!cred->groups)
        return -EACCES;

    for (i = 0; i < cred->ngroups; i++)
    {
        if (cred->groups[i] == cred->egid)
            continue;
        if (dac_class_allows(st, mask, cred->euid, cred->groups[i]))
            return 0;
    }

    return -EACCES;
}
