/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: credentials.c
 * Description: Current-task credential accessors.
 */

#include <ir0/process.h>
#include <ir0/cred.h>

const struct ir0_task_cred *ir0_current_cred(void)
{
    static struct ir0_task_cred cred;
    static struct ir0_task_cred boot_root = {
        .uid = 0,
        .gid = 0,
        .euid = 0,
        .egid = 0,
        .umask = 0022,
        .pid = 0,
        .cwd = "/",
        .groups = NULL,
        .ngroups = 0,
    };

    if (!current_process)
        return &boot_root;

    cred.uid = (uid_t)current_process->uid;
    cred.gid = (gid_t)current_process->gid;
    cred.euid = (uid_t)current_process->euid;
    cred.egid = (gid_t)current_process->egid;
    cred.umask = (mode_t)current_process->umask;
    cred.pid = current_process->task.pid;
    cred.cwd = current_process->cwd;
    cred.groups = current_process->groups;
    cred.ngroups = (int)current_process->ngroups;

    return &cred;
}

pid_t ir0_current_pid(void)
{
    const struct ir0_task_cred *c;

    c = ir0_current_cred();
    return c ? c->pid : (pid_t)0;
}

int ir0_cred_is_root(void)
{
    const struct ir0_task_cred *c;

    c = ir0_current_cred();
    return (c && c->euid == ROOT_UID) ? 1 : 0;
}
