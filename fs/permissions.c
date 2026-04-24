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
#include "process.h"
#include <fs/vfs.h>
#include <fs/minix_fs.h>
#include <ir0/stat.h>
#include <string.h>

struct simple_user_entry {
    uid_t uid;
    gid_t gid;
    const char *name;
    const char *password;
};

static const struct simple_user_entry simple_users[] = {
    { ROOT_UID, ROOT_GID, "root", "root" },
    { USER_UID, USER_GID, "user", "ir0" },
};

static const struct simple_user_entry *find_user_by_uid(uid_t uid)
{
    for (size_t i = 0; i < (sizeof(simple_users) / sizeof(simple_users[0])); i++)
    {
        if (simple_users[i].uid == uid)
            return &simple_users[i];
    }
    return NULL;
}

/* Initialize simple user system */
void init_simple_users(void)
{
    /* Nothing to initialize - everything is hardcoded */
}

/* Get current process UID */
uint32_t get_current_uid(void)
{
    return current_process ? current_process->euid : ROOT_UID;
}

/* Get current process GID */
uint32_t get_current_gid(void)
{
    return current_process ? current_process->egid : ROOT_GID;
}

/* Check if process is root */
bool is_root(const struct process *process)
{
    return process && process->euid == ROOT_UID;
}

/* Check file access permissions - Unix style */
bool check_file_access(const char *path, int mode, const struct process *process)
{
    if (!path || !process)
        return false;

    /* Root can do everything */
    if (process->euid == ROOT_UID)
        return true;

    /* Get file stats */
    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return false;

    uint16_t file_mode = st.st_mode;
    uint32_t file_uid = st.st_uid;
    uint32_t file_gid = st.st_gid;

    /* Check owner permissions */
    if (process->euid == file_uid) {
        if ((mode & ACCESS_READ) && !(file_mode & S_IRUSR))
            return false;
        if ((mode & ACCESS_WRITE) && !(file_mode & S_IWUSR))
            return false;
        if ((mode & ACCESS_EXEC) && !(file_mode & S_IXUSR))
            return false;
        return true;
    }

    /* Check group permissions */
    if (process->egid == file_gid) {
        if ((mode & ACCESS_READ) && !(file_mode & S_IRGRP))
            return false;
        if ((mode & ACCESS_WRITE) && !(file_mode & S_IWGRP))
            return false;
        if ((mode & ACCESS_EXEC) && !(file_mode & S_IXGRP))
            return false;
        return true;
    }

    /* Check other permissions */
    if ((mode & ACCESS_READ) && !(file_mode & S_IROTH))
        return false;
    if ((mode & ACCESS_WRITE) && !(file_mode & S_IWOTH))
        return false;
    if ((mode & ACCESS_EXEC) && !(file_mode & S_IXOTH))
        return false;

    return true;
}

bool user_exists(uid_t uid)
{
    return find_user_by_uid(uid) != NULL;
}

const char *lookup_user_name(uid_t uid)
{
    const struct simple_user_entry *entry = find_user_by_uid(uid);
    return entry ? entry->name : NULL;
}

int auth_user_password(uid_t uid, const char *password)
{
    const struct simple_user_entry *entry = find_user_by_uid(uid);
    if (!entry || !password)
        return -1;
    return strcmp(entry->password, password) == 0 ? 0 : -1;
}
