/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs_namei.c
 * Description: VFS path operations — stat, mkdir, unlink, link,
 *              symlink, readlink, rename, rmdir, readdir, chown,
 *              chmod, truncate and utimens.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "vfs.h"
#include <ir0/path.h>
#include <ir0/kmem.h>
#include <ir0/named_fifo.h>
#include <ir0/permissions.h>
#include <ir0/credentials.h>
#include <ir0/process.h>
#include <string.h>
#include "vfs_internal.h"

static int build_path(char *dest, size_t sz, const char *dir, const char *name)
{
    if (!dest || !dir || !name || sz == 0)
        return -EINVAL;
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    if (dlen + nlen + 2 > sz)
        return -ENAMETOOLONG;
    size_t i = 0;
    for (size_t j = 0; j < dlen && i < sz - 1; j++)
        dest[i++] = dir[j];
    if (dlen > 0 && dir[dlen - 1] != '/' && i < sz - 1)
        dest[i++] = '/';
    for (size_t j = 0; j < nlen && i < sz - 1; j++)
        dest[i++] = name[j];
    dest[i] = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Path operations                                                    */
/* ------------------------------------------------------------------ */

int vfs_stat(const char *path, stat_t *buf)
{
    if (!path || !buf)
        return -EINVAL;
    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->stat)
        return -ENODEV;
    memset(buf, 0, sizeof(*buf));
    return vfs_ops_stat(ops, path, buf);
}

int vfs_mkdir(const char *path, int mode)
{
    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    if (vfs_mount_is_readonly(path))
        return -EROFS;

    char parent[MAX_PATH];
    if (vfs_parent_dir(path, parent, sizeof(parent)) == 0)
        if (strcmp(parent, "/") != 0)
            if (!ir0_check_file_access(parent, ACCESS_WRITE))
                return -EACCES;

    if (strcmp(path, "/") == 0)
        return -EEXIST;

    stat_t st;
    if (vfs_stat(path, &st) == 0 && (st.st_mode & S_IFMT) != 0)
        return -EEXIST;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->mkdir)
        return -ENOSYS;
    return vfs_ops_mkdir(ops, path, (mode_t)mode);
}

int vfs_unlink(const char *path)
{
    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    if (vfs_mount_is_readonly(path))
        return -EROFS;
    if (strcmp(path, "/") == 0)
        return -EPERM;

    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return -ENOENT;

    if (S_ISDIR(st.st_mode))
        return -EISDIR;

    if (!ir0_check_file_access(path, ACCESS_WRITE))
        return -EACCES;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->unlink)
        return -ENOSYS;
    return vfs_ops_unlink(ops, path);
}

int vfs_link(const char *oldpath, const char *newpath)
{
    int ret = vfs_validate_path(oldpath);
    if (ret != 0) return ret;
    ret = vfs_validate_path(newpath);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    stat_t st;
    if (vfs_stat(oldpath, &st) != 0)
        return -ENOENT;
    if (!ir0_check_file_access(oldpath, ACCESS_READ))
        return -EACCES;

    char parent[MAX_PATH];
    if (vfs_parent_dir(newpath, parent, sizeof(parent)) == 0)
        if (!ir0_check_file_access(parent, ACCESS_WRITE))
            return -EACCES;

    if (vfs_stat(newpath, &st) == 0)
        return -EEXIST;

    struct vfs_ops *ops = vfs_ops_for_path(oldpath);
    if (!ops || !ops->link)
        return -ENOSYS;
    return vfs_ops_link(ops, oldpath, newpath);
}

int vfs_symlink(const char *target, const char *linkpath)
{
    int ret;
    struct vfs_ops *ops;

    if (!target || !linkpath)
        return -EINVAL;
    ret = vfs_validate_path(linkpath);
    if (ret != 0)
        return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    ops = vfs_ops_for_path(linkpath);
    if (!ops || !ops->symlink)
        return -ENOSYS;
    return vfs_ops_symlink(ops, target, linkpath);
}

int vfs_readlink(const char *path, char *buf, size_t buflen)
{
    int ret;
    struct vfs_ops *ops;

    if (!path || !buf || buflen == 0)
        return -EINVAL;
    ret = vfs_validate_path(path);
    if (ret != 0)
        return ret;
    ops = vfs_ops_for_path(path);
    if (!ops || !ops->readlink)
        return -ENOSYS;
    return vfs_ops_readlink(ops, path, buf, buflen);
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    int ret = vfs_validate_path(oldpath);
    if (ret != 0) return ret;
    ret = vfs_validate_path(newpath);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    stat_t st_old;
    int retry_rename;

    if (vfs_stat(oldpath, &st_old) != 0)
        return -ENOENT;

    retry_rename = 0;
    if (S_ISDIR(st_old.st_mode))
    {
        if (named_fifo_is_runsv_supervise_regular_path(oldpath))
        {
            ret = vfs_clear_stale_for_regular_file(oldpath);
            if (ret != 0)
                return ret;
            retry_rename = 1;
        }
        else
            return -EISDIR;
    }

    if (retry_rename)
    {
        if (vfs_stat(oldpath, &st_old) != 0)
            return -ENOENT;
        if (S_ISDIR(st_old.st_mode))
            return -EISDIR;
    }

    if (!ir0_check_file_access(oldpath, ACCESS_READ | ACCESS_WRITE))
        return -EACCES;

    char parent[MAX_PATH];
    if (vfs_parent_dir(newpath, parent, sizeof(parent)) == 0)
        if (!ir0_check_file_access(parent, ACCESS_WRITE))
            return -EACCES;

    struct vfs_mount *m_old = vfs_find_mount(oldpath);
    struct vfs_mount *m_new = vfs_find_mount(newpath);
    if (m_old != m_new)
        return -EXDEV;

    stat_t st_new;

    if (vfs_stat(newpath, &st_new) == 0)
    {
        if (S_ISDIR(st_new.st_mode))
        {
            ret = vfs_rmdir_recursive(newpath);
            if (ret != 0)
                return ret;
        }
        else
        {
            ret = vfs_unlink(newpath);
            if (ret != 0)
                return ret;
        }
    }

    struct vfs_ops *ops = vfs_ops_for_path(oldpath);
    if (!ops)
        return -ENOSYS;
    if (ops->rename)
        return vfs_ops_rename(ops, oldpath, newpath);
    if (!ops->link)
        return -ENOSYS;
    ret = vfs_ops_link(ops, oldpath, newpath);
    if (ret != 0)
        return ret;
    return vfs_unlink(oldpath);
}

int vfs_rmdir(const char *path)
{
    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    char norm[MAX_PATH];
    if (normalize_path(path, norm, sizeof(norm)) != 0)
        return -ENAMETOOLONG;
    if (strcmp(norm, "/") == 0)
        return -EPERM;

    stat_t st;
    if (vfs_stat(norm, &st) != 0)
        return -ENOENT;
    if (!S_ISDIR(st.st_mode))
        return -ENOTDIR;

    ret = vfs_check_dir_traverse(norm);
    if (ret != 0)
        return ret;

    char parent[MAX_PATH];
    if (vfs_parent_dir(norm, parent, sizeof(parent)) == 0)
    {
        if (!ir0_check_file_access(parent, ACCESS_WRITE | ACCESS_EXEC))
            return -EACCES;
    }

    struct vfs_ops *ops = vfs_ops_for_path(norm);
    if (!ops || !ops->rmdir)
        return -ENOSYS;
    return vfs_ops_rmdir(ops, norm);
}

static int rmdir_recursive_impl(const char *path, int depth)
{
    if (depth > 32)
        return -ELOOP;

    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;

    char norm[MAX_PATH];
    if (normalize_path(path, norm, sizeof(norm)) != 0)
        return -ENAMETOOLONG;
    if (norm[0] == '/' && norm[1] == '\0')
        return -EPERM;

    stat_t st;
    if (vfs_stat(norm, &st) != 0)
        return -ENOENT;
    if (!S_ISDIR(st.st_mode))
        return vfs_unlink(norm);

    /*
     * The directory entries and the two path buffers live on the heap, not
     * on the stack. This function recurses once per directory level, and on
     * the stack the same locals made each frame 4 KiB: the depth limit of 32
     * above would then need 128 KiB, four times the 32 KiB kernel stack. A
     * deep tree walked into the guard page and the fault escalated to a
     * double fault. Off the stack the frame is a few hundred bytes, so the
     * limit that the code already declares is one the stack can honour.
     */
    struct rmdir_scratch {
        struct vfs_dirent entries[32];
        char full[MAX_PATH];
        char resolved[MAX_PATH];
    } *scr = kmalloc(sizeof(*scr));
    struct vfs_ops *ops;
    size_t nlen;
    int n;
    int i;
    int ret_code;

    if (!scr)
        return -ENOMEM;

    n = vfs_readdir(norm, scr->entries, 32);
    if (n < 0) {
        ops = vfs_ops_for_path(norm);
        ret_code = (ops && ops->rmdir) ? vfs_ops_rmdir(ops, norm) : n;
        goto out;
    }

    nlen = strlen(norm);
    for (i = 0; i < n; i++) {
        stat_t est;
        size_t rlen;

        if (scr->entries[i].name[0] == '\0')
            continue;
        if (scr->entries[i].name[0] == '.' &&
            (scr->entries[i].name[1] == '\0' ||
             (scr->entries[i].name[1] == '.' && scr->entries[i].name[2] == '\0')))
            continue;

        if (build_path(scr->full, sizeof(scr->full), norm, scr->entries[i].name) != 0)
            continue;
        if (normalize_path(scr->full, scr->resolved, sizeof(scr->resolved)) != 0)
            continue;
        if (scr->resolved[0] == '/' && scr->resolved[1] == '\0')
            continue;
        if (strcmp(scr->resolved, norm) == 0)
            continue;

        rlen = strlen(scr->resolved);
        if (rlen < nlen)
            continue;
        if (rlen <= nlen || strncmp(scr->resolved, norm, nlen) != 0 ||
            scr->resolved[nlen] != '/')
            continue;

        if (vfs_stat(scr->resolved, &est) == 0) {
            int rc;

            if (S_ISDIR(est.st_mode))
                rc = rmdir_recursive_impl(scr->resolved, depth + 1);
            else
                rc = vfs_unlink(scr->resolved);
            if (rc != 0) {
                ret_code = rc;
                goto out;
            }
        }
    }

    ops = vfs_ops_for_path(norm);
    ret_code = (ops && ops->rmdir) ? vfs_ops_rmdir(ops, norm) : -ENOSYS;

out:
    kfree(scr);
    return ret_code;
}

int vfs_rmdir_recursive(const char *path)
{
    return rmdir_recursive_impl(path, 0);
}

/*
 * vfs_clear_stale_for_regular_file - Remove wrong-type VFS nodes blocking O_CREAT.
 * Clears empty/non-empty directories and non-regular files so a regular file or
 * named FIFO can be created at @path (runit supervise/pid.new, mkfifo paths).
 */
int vfs_clear_stale_for_regular_file(const char *path)
{
    stat_t st;
    int rc;

    if (!path)
        return -EINVAL;

    for (;;)
    {
        if (vfs_stat(path, &st) != 0)
            return 0;
        if (S_ISREG(st.st_mode))
        {
            rc = vfs_unlink(path);
            if (rc == -EISDIR)
                rc = vfs_rmdir_recursive(path);
            if (rc != 0)
                return rc;
            continue;
        }
        if (S_ISFIFO(st.st_mode))
            return 0;
        if (S_ISDIR(st.st_mode))
        {
            rc = vfs_rmdir_recursive(path);
            if (rc != 0)
                return rc;
            continue;
        }
        rc = vfs_unlink(path);
        if (rc == -EISDIR)
            rc = vfs_rmdir_recursive(path);
        if (rc != 0)
            return rc;
    }
}

int vfs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
    if (!path || !entries || max <= 0)
        return -EINVAL;
    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    if (!ir0_check_file_access(path, ACCESS_EXEC))
        return -EACCES;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->readdir)
        return -ENOSYS;
    return vfs_ops_readdir(ops, path, entries, max);
}

int vfs_chown(const char *path, uid_t owner, gid_t group)
{
    if (!path) return -EINVAL;
    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;
    if (!ir0_cred_is_root())
        return -EPERM;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->chown)
        return -ENOSYS;
    return vfs_ops_chown(ops, path, owner, group);
}

int vfs_chmod(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;
    int ret = vfs_validate_path(path);
    if (ret != 0)
        return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    stat_t st;
    ret = vfs_stat(path, &st);
    if (ret != 0)
        return ret;

    if (!ir0_cred_is_root() && ir0_current_cred()->euid != st.st_uid)
        return -EPERM;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->chmod)
        return -ENOSYS;
    return vfs_ops_chmod(ops, path, mode);
}

int vfs_truncate(const char *path, size_t length)
{
    if (!path)
        return -EINVAL;
    if (!ir0_current_cred())
        return -ESRCH;
    int ret = vfs_validate_path(path);
    if (ret != 0)
        return ret;
    if (!ir0_check_file_access(path, ACCESS_WRITE))
        return -EACCES;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->truncate)
        return -ENOSYS;
    return vfs_ops_truncate(ops, path, length);
}

int vfs_utimens(const char *path, const struct timespec times[2])
{
    struct vfs_ops *ops;
    stat_t st;

    if (!path)
        return -EINVAL;
    if (!ir0_current_cred())
        return -ESRCH;
    if (vfs_validate_path(path) != 0)
        return -EINVAL;
    if (vfs_stat(path, &st) != 0)
        return -ENOENT;

    ops = vfs_ops_for_path(path);
    if (!ops || !ops->utimens)
        return -ENOSYS;
    return vfs_ops_utimens(ops, path, times);
}
