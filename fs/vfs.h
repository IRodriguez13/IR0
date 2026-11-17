/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs.h
 * Description: IR0 kernel source/header file
 */

/* vfs.h - Minimal path-based Virtual File System */
#pragma once

#include <ir0/stat.h>
#include <ir0/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>
#include <ir0/vfs_backend.h>
#include <ir0/vfs_file.h>
#include <ir0/statfs.h>

struct vfs_dirent;

#define VFS_PATH_MAX IR0_VFS_PATH_MAX
#define VFS_NAME_MAX 255

/* struct vfs_ops — see includes/ir0/vfs_backend.h */

/* vfs_fstype.fs_flags */
#define FS_NOSUID_DEFAULT 1 /* new mounts start MS_NOSUID (pseudo, in-RAM) */

/*
 * Filesystem type descriptor — one per FS driver (Linux file_system_type).
 * @alias:  second type name mount(2) accepts for the same driver, or NULL.
 * @statfs: statfs(2) for @path on a mount of this type; NULL reports an
 *          empty filesystem. Called under the fs-op lock.
 */
struct vfs_fstype {
    const char *name;
    const char *alias;
    int fs_flags;
    struct vfs_ops *ops;
    int (*mount)(const char *dev, const char *dir);
    int (*umount)(const char *dir);
    int (*statfs)(const char *path, struct ir0_statfs *buf);
    struct vfs_fstype *next;
};

/* Mount table entry */
struct vfs_mount {
    char path[VFS_PATH_MAX];
    char backend_path[VFS_PATH_MAX];
    char dev[64];
    struct vfs_fstype *fs;
    unsigned long flags; /* MS_RDONLY and friends */
    struct vfs_mount *next;
};

/* Linux mount(2) flag bits we honour today (values match linux/fs.h). */
#define IR0_MS_RDONLY  1
#define IR0_MS_NOSUID  2
#define IR0_MS_REMOUNT 32
#define IR0_MS_BIND    4096
/* Flags that are not implemented yet — reject on new mounts. */
#define IR0_MS_UNSUPPORTED (IR0_MS_BIND)

int vfs_remount(const char *path, unsigned long flags);

/* Directory entry returned by vfs_readdir */
struct vfs_dirent {
    char name[VFS_PATH_MAX];
    uint8_t type;
};

/*
 * d_type values for vfs_dirent (Linux getdents subset, stable ABI).
 */
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
#define DT_LNK     10

/* Lifecycle */
int vfs_mount(const char *dev, const char *path, const char *fstype);
int vfs_umount(const char *path);
int vfs_pivot_root(const char *new_root, const char *put_old);
int vfs_init_root(void);

/*
 * The block device a mount source names: a registered block device
 * ("/dev/<disk>" or "<disk>"), or partition n of one ("<disk><n>").
 * @start is the partition's first sector, 0 for a whole disk.
 */
struct vfs_bdev {
    char disk[16];
    uint64_t start;
};

/*
 * Linux lookup_bdev() for block filesystems: fill @bdev for @source. 0, or
 * -ENOENT (no such path), -ENOTBLK (the path is not a block device),
 * -ENXIO (a block node with no registered device), -ENODEV (no such
 * partition) or -EINVAL (no source).
 */
int lookup_bdev(const char *source, struct vfs_bdev *bdev);

/* Mount table (single linked list, newest first); NULL if empty */
struct vfs_mount *vfs_get_mounts(void);

/* Best-effort sync: flush all registered block devices (FS-level sync later). */
int vfs_sync(void);

/* File operations — @flags must be IR0_O_* (see includes/ir0/open_flags.h) */
int vfs_open(const char *path, int flags, mode_t mode, struct vfs_file **out);
int vfs_read(struct vfs_file *f, char *buf, size_t count);
int vfs_write(struct vfs_file *f, const char *buf, size_t count);
int vfs_pread(struct vfs_file *f, char *buf, size_t count, off_t offset);
int vfs_pwrite(struct vfs_file *f, const char *buf, size_t count, off_t offset);
int vfs_close(struct vfs_file *f);
off_t vfs_lseek(struct vfs_file *f, off_t offset, int whence);

/* Path operations */
int vfs_stat(const char *path, stat_t *buf);
struct ir0_statfs;
int vfs_mkdir(const char *path, int mode);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_link(const char *oldpath, const char *newpath);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_readlink(const char *path, char *buf, size_t buflen);
int vfs_readdir(const char *path, struct vfs_dirent *entries, int max);
int vfs_chown(const char *path, uid_t owner, gid_t group);
int vfs_chmod(const char *path, mode_t mode);
int vfs_truncate(const char *path, size_t length);
int vfs_utimens(const char *path, const struct timespec times[2]);

/*
 * Linux may_create(): may the current task add the name @path? 0, -EACCES
 * (no write and search on the parent or no search on the way there) or
 * the lookup error of the parent. Callers check EEXIST first. Used by
 * mkdir, link, symlink, mknod and open(O_CREAT) of a new file.
 */
int may_create(const char *path);

/*
 * Linux may_delete(): may the current task remove the name @path that
 * refers to @victim? 0, -EACCES (no write and search on the parent or on
 * the way there), -EPERM (sticky parent), -ENOTDIR/-EISDIR (@victim does
 * not match @isdir) or the lookup error. The VFS calls it for unlink,
 * rmdir and rename; in-memory nodes (named FIFOs, sockets, device nodes,
 * symlinks) call it before they drop a name.
 */
int may_delete(const char *path, const stat_t *victim, bool isdir);

/* Bulk read helper (ELF loader, etc.) */
int vfs_read_file(const char *path, void **data, size_t *size);

/* Non-zero when the mount holding @path may raise privileges on exec. */
int vfs_path_allows_setid(const char *path);

/* Exec-loader observational audit (single-threaded). */
void vfs_exec_audit_begin(const char *path);
void vfs_exec_audit_end(void);
int vfs_exec_audit_is_active(void);
