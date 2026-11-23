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
#include <ir0/chmod.h>
#include <ir0/cred.h>
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

/*
 * may_create - Linux fs/namei.c may_create(): may the caller add the name
 * @path? A new name changes the parent directory: it needs search on
 * every directory on the way and write and search on the parent, which
 * must exist (ENOENT) and be a directory (ENOTDIR). The caller has
 * checked that the name is free: in Linux EEXIST comes first.
 */
int may_create(const char *path)
{
    const struct ir0_task_cred *cred = ir0_current_cred();
    char parent[MAX_PATH];
    stat_t dir;
    int ret;

    if (!cred)
        return -ESRCH;
    ret = vfs_check_dir_traverse(path);
    if (ret != 0)
        return ret;
    ret = vfs_parent_dir(path, parent, sizeof(parent));
    if (ret != 0)
        return ret;
    ret = vfs_stat(parent, &dir);
    if (ret != 0)
        return ret;
    if ((dir.st_mode & S_IFMT) != 0 && !S_ISDIR(dir.st_mode))
        return -ENOTDIR;
    if (generic_permission(cred, &dir, ACCESS_WRITE | ACCESS_EXEC) != 0)
        return -EACCES;
    return 0;
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
    stat_t st;
    ret = vfs_lookup_parent(path, parent, sizeof(parent));
    if (ret != 0)
        return ret;

    if (strcmp(path, "/") == 0)
        return -EEXIST;

    if (vfs_stat(path, &st) == 0 && (st.st_mode & S_IFMT) != 0)
        return -EEXIST;

    ret = may_create(path);
    if (ret != 0)
        return ret;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops || !ops->mkdir)
        return -ENOSYS;
    return vfs_ops_mkdir(ops, path, (mode_t)mode);
}

/*
 * may_delete - Linux fs/namei.c may_delete(): may the caller remove the
 * name @path, which refers to @victim? Removing a name changes the parent
 * directory, never the victim: it needs search on every directory on the
 * way and write and search on the parent. In a sticky (+t) parent only
 * the owner of the victim, the owner of the parent or root may remove it
 * (check_sticky(), EPERM). Last, @victim must be a directory when @isdir
 * is set (ENOTDIR) and must not be one otherwise (EISDIR).
 */
int may_delete(const char *path, const stat_t *victim, bool isdir)
{
    const struct ir0_task_cred *cred = ir0_current_cred();
    char parent[MAX_PATH];
    stat_t dir;
    int ret;

    if (!cred)
        return -ESRCH;
    ret = vfs_check_dir_traverse(path);
    if (ret != 0)
        return ret;
    ret = vfs_parent_dir(path, parent, sizeof(parent));
    if (ret != 0)
        return ret;
    ret = vfs_stat(parent, &dir);
    if (ret != 0)
        return ret;
    if (generic_permission(cred, &dir, ACCESS_WRITE | ACCESS_EXEC) != 0)
        return -EACCES;
    if ((dir.st_mode & S_ISVTX) && cred->euid != ROOT_UID &&
        cred->euid != victim->st_uid && cred->euid != dir.st_uid)
        return -EPERM;
    if (isdir && !S_ISDIR(victim->st_mode))
        return -ENOTDIR;
    if (!isdir && S_ISDIR(victim->st_mode))
        return -EISDIR;
    return 0;
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

    ret = may_delete(path, &st, false);
    if (ret != 0)
        return ret;

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
    if (generic_permission(ir0_current_cred(), &st, ACCESS_READ) != 0)
        return -EACCES;

    if (vfs_stat(newpath, &st) == 0)
        return -EEXIST;
    ret = may_create(newpath);
    if (ret != 0)
        return ret;

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
    ret = may_create(linkpath);
    if (ret != 0)
        return ret;
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

/*
 * Nonzero when the directory @path lists any entry besides "." and "..".
 * It asks the backend directly: replacing a directory needs no read
 * permission on it.
 */
static int vfs_dir_has_entries(const char *path)
{
    struct vfs_ops *ops = vfs_ops_for_path(path);
    struct vfs_dirent *ents;
    int n;
    int i;
    int found = 0;

    if (!ops || !ops->readdir)
        return -ENOSYS;
    ents = kmalloc(3 * sizeof(*ents));
    if (!ents)
        return -ENOMEM;
    n = vfs_ops_readdir(ops, path, ents, 3);
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
 *
 * Permissions follow vfs_rename() in fs/namei.c: the old name is deleted
 * and the new one created, or deleted first when it exists, each with the
 * check of its own directory (may_delete(), may_create()); the file itself
 * needs nothing. A directory that moves to another parent also needs
 * write on itself, for its ".." entry.
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
    bool isdir;
    char old_parent[MAX_PATH];
    char new_parent[MAX_PATH];
    struct vfs_ops *ops;

    if (vfs_stat(oldpath, &st_old) != 0)
        return -ENOENT;

    struct vfs_mount *m_old = vfs_find_mount(oldpath);
    struct vfs_mount *m_new = vfs_find_mount(newpath);
    if (m_old != m_new)
        return -EXDEV;

    have_new = vfs_stat(newpath, &st_new) == 0;
    if (have_new &&
        (strcmp(oldpath, newpath) == 0 ||
         (st_old.st_ino != 0 && st_old.st_ino == st_new.st_ino &&
          st_old.st_dev == st_new.st_dev)))
        return 0;

    isdir = S_ISDIR(st_old.st_mode);
    ret = may_delete(oldpath, &st_old, isdir);
    if (ret != 0)
        return ret;
    ret = have_new ? may_delete(newpath, &st_new, isdir) : may_create(newpath);
    if (ret != 0)
        return ret;
    if (isdir)
    {
        ret = vfs_parent_dir(oldpath, old_parent, sizeof(old_parent));
        if (ret == 0)
            ret = vfs_parent_dir(newpath, new_parent, sizeof(new_parent));
        if (ret != 0)
            return ret;
        if (strcmp(old_parent, new_parent) != 0 &&
            generic_permission(ir0_current_cred(), &st_old,
                               ACCESS_WRITE) != 0)
            return -EACCES;
    }

    if (have_new && S_ISDIR(st_new.st_mode))
    {
        ret = vfs_dir_has_entries(newpath);
        if (ret != 0)
            return ret < 0 ? ret : -ENOTEMPTY;
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
    vfs_ops_renamed(ops, oldpath, newpath);
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

    ret = may_delete(norm, &st, true);
    if (ret != 0)
        return ret;

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

    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return -EACCES;
    if ((st.st_mode & S_IFMT) != 0 && !S_ISDIR(st.st_mode))
        return -ENOTDIR;
    /* Listing a directory reads it; search only lets a path go through. */
    if (generic_permission(ir0_current_cred(), &st, ACCESS_READ) != 0)
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
    stat_t st;
    if (vfs_stat(path, &st) != 0 ||
        generic_permission(ir0_current_cred(), &st, ACCESS_WRITE) != 0)
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
