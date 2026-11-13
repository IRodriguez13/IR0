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
#include <ir0/permissions.h>
#include <ir0/credentials.h>
#include <ir0/process.h>
#include <string.h>
#include "vfs_internal.h"

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
    ret = vfs_ops_stat(ops, path, buf);
    if (ret == -ENOENT)
        ret = vfs_lookup_error(path);
    return ret;
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
    ret = vfs_lookup_parent(path, parent, sizeof(parent));
    if (ret != 0)
        return ret;
    if (strcmp(parent, "/") != 0 && !ir0_check_file_access(parent, ACCESS_WRITE))
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

/* Nonzero when the directory @path lists any entry besides "." and "..". */
static int vfs_dir_has_entries(const char *path)
{
    struct vfs_dirent *ents = kmalloc(3 * sizeof(*ents));
    int n;
    int i;
    int found = 0;

    if (!ents)
        return -ENOMEM;
    n = vfs_readdir(path, ents, 3);
    for (i = 0; i < n && !found; i++)
        found = strcmp(ents[i].name, ".") != 0 && strcmp(ents[i].name, "..") != 0;
    kfree(ents);
    return n < 0 ? n : found;
}

/*
 * vfs_rename - Linux rename(2) within one mount. An existing target is
 * replaced only by an object of the same kind: a non-directory over a
 * directory is EISDIR, a directory over a non-directory ENOTDIR, and a
 * directory over a non-empty directory ENOTEMPTY. Two names for the same
 * file leave both in place.
 */
int vfs_rename(const char *oldpath, const char *newpath)
{
    int ret = vfs_validate_path(oldpath);
    if (ret != 0) return ret;
    ret = vfs_validate_path(newpath);
    if (ret != 0) return ret;
    if (!ir0_current_cred())
        return -ESRCH;

    stat_t st_old;
    stat_t st_new;
    int have_new;
    struct vfs_ops *ops;

    if (vfs_stat(oldpath, &st_old) != 0)
        return -ENOENT;

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

    have_new = vfs_stat(newpath, &st_new) == 0;
    if (have_new)
    {
        if (strcmp(oldpath, newpath) == 0 ||
            (st_old.st_ino != 0 && st_old.st_ino == st_new.st_ino &&
             st_old.st_dev == st_new.st_dev))
            return 0;
        if (S_ISDIR(st_old.st_mode) && !S_ISDIR(st_new.st_mode))
            return -ENOTDIR;
        if (!S_ISDIR(st_old.st_mode) && S_ISDIR(st_new.st_mode))
            return -EISDIR;
        if (S_ISDIR(st_new.st_mode))
        {
            ret = vfs_dir_has_entries(newpath);
            if (ret != 0)
                return ret < 0 ? ret : -ENOTEMPTY;
        }
    }

    ops = vfs_ops_for_path(oldpath);
    if (!ops)
        return -ENOSYS;
    /* link + unlink cannot move a directory; only a backend rename can. */
    if (S_ISDIR(st_old.st_mode) && !ops->rename)
        return -EISDIR;

    if (have_new)
    {
        ret = S_ISDIR(st_new.st_mode) ? vfs_rmdir(newpath) : vfs_unlink(newpath);
        if (ret != 0)
            return ret;
    }

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
