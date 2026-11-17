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

/* in_group_p(): @gid is the effective group or a supplementary group. */
static bool cred_in_group(const struct ir0_task_cred *cred, gid_t gid)
{
    int i;

    if (cred->egid == gid)
        return true;
    for (i = 0; cred->groups && i < cred->ngroups; i++)
    {
        if (cred->groups[i] == gid)
            return true;
    }
    return false;
}

int generic_permission(const struct ir0_task_cred *cred, const stat_t *st,
                       int mask)
{
    unsigned int rwx;
    unsigned int want;

    if (!cred || !st)
        return -EACCES;

    /*
     * acl_permission_check(): the first class that matches decides. The
     * owner gets the owner bits and a member of the file group the group
     * bits, even when the other bits would allow more.
     */
    if (cred->euid == st->st_uid)
        rwx = (st->st_mode >> 6) & 7;
    else if (cred_in_group(cred, st->st_gid))
        rwx = (st->st_mode >> 3) & 7;
    else
        rwx = st->st_mode & 7;

    want = 0;
    if (mask & ACCESS_READ)
        want |= 4;
    if (mask & ACCESS_WRITE)
        want |= 2;
    if (mask & ACCESS_EXEC)
        want |= 1;
    if (!(want & ~rwx))
        return 0;

    /*
     * CAP_DAC_OVERRIDE: root reads, writes and searches anything, but
     * executes a non-directory only when one of its exec bits is set.
     */
    if (cred->euid == ROOT_UID &&
        (!(mask & ACCESS_EXEC) || S_ISDIR(st->st_mode) ||
         (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))))
        return 0;

    return -EACCES;
}
