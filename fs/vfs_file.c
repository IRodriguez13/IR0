/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs_file.c
 * Description: VFS open files — open, read, write, seek and the
 *              whole-file read used by exec.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "vfs.h"
#include <ir0/kmem.h>
#include <ir0/fcntl.h>
#include <ir0/flock.h>
#include <ir0/exec_read_trace.h>
#include <ir0/permissions.h>
#include <ir0/credentials.h>
#include <ir0/klog.h>
#include <ir0/overflow.h>
#include <ir0/process.h>
#include "vfs_internal.h"

#define VFS_READ_FILE_MAX_BYTES (64U * 1024U * 1024U)

static int vfs_exec_audit_active;
static const char *vfs_exec_audit_path;

void vfs_exec_audit_begin(const char *path)
{
    /* Exec ATA dump retired; KTM covers validation. */
    (void)path;
    vfs_exec_audit_active = 0;
    vfs_exec_audit_path = NULL;
}

void vfs_exec_audit_end(void)
{
    vfs_exec_audit_active = 0;
    vfs_exec_audit_path = NULL;
}

int vfs_exec_audit_is_active(void)
{
    return vfs_exec_audit_active;
}

static void vfs_exec_audit_log(const char *stage, const char *path, int ret,
                               off_t offset, size_t req, size_t got,
                               uint64_t st_ino, int64_t st_size)
{
    if (!vfs_exec_audit_active)
        return;

    klog_print("[EXEC_AUDIT][VFS] stage=");
    klog_print(stage ? stage : "?");
    klog_print(" path=");
    klog_print(path ? path : "(null)");
    klog_print(" ret=");
    klog_hex64((uint64_t)(int64_t)ret);
    klog_print(" offset=");
    klog_hex64((uint64_t)offset);
    klog_print(" req=");
    klog_hex64((uint64_t)req);
    klog_print(" got=");
    klog_hex64((uint64_t)got);
    klog_print(" st_ino=");
    klog_hex64(st_ino);
    klog_print(" st_size=");
    klog_hex64((uint64_t)st_size);
    klog_print("\n");
}

/* ------------------------------------------------------------------ */
/*  File operations                                                    */
/* ------------------------------------------------------------------ */

int vfs_open(const char *path, int flags, mode_t mode, struct vfs_file **out)
{
    int ret = vfs_validate_path(path);
    if (ret != 0) return ret;
    if (!out)     return -EFAULT;
    if (!ir0_current_cred())
        return -ESRCH;

    if (!ir0_open_flags_ok_for_vfs(flags))
    {
        if (DEBUG_VFS)
            klog_info("VFS", "CLASSIFY VFS_LINUX_RAW_FLAGS_REJECTED");
        return -EINVAL;
    }

    if (vfs_mount_is_readonly(path) &&
        ((flags & IR0_O_ACCMODE) != IR0_O_RDONLY || (flags & IR0_O_CREAT)))
        return -EROFS;

    ret = vfs_check_dir_traverse(path);
    if (ret != 0)
        return ret;

    struct vfs_ops *ops = vfs_ops_for_path(path);
    if (!ops)
        return -ENODEV;

    if (flags & IR0_O_CREAT)
    {
        char parent[MAX_PATH];
        stat_t st;
        int target_exists;

        if (vfs_parent_dir(path, parent, sizeof(parent)) == 0)
        {
            if (!ops->stat || vfs_ops_stat(ops, parent, &st) != 0)
                return -ENOENT;
            if (!ir0_check_file_access(parent, ACCESS_WRITE))
                return -EACCES;
        }

        target_exists = (ops->stat && vfs_ops_stat(ops, path, &st) == 0) ? 1 : 0;

        if (target_exists && S_ISDIR(st.st_mode) && !(flags & IR0_O_DIRECTORY))
        {
            ret = vfs_rmdir_recursive(path);
            if (ret != 0)
                return ret;
            target_exists = 0;
        }

        if (!target_exists)
        {
            if (ops->create)
            {
                /* open(2): the caller's mode is authoritative, 0000 included. */
                mode_t fmode = mode & 07777;

                ret = vfs_ops_create(ops, path, fmode);
                if (ret == -EISDIR)
                {
                    ret = vfs_rmdir_recursive(path);
                    if (ret == 0)
                        ret = vfs_ops_create(ops, path, fmode);
                }
                if (ret != 0)
                    return ret;
                if (DEBUG_VFS)
                    klog_info("VFS", "CLASSIFY VFS_CREATE_SEMANTICS_GENERIC");
            }
            else
            {
                return -ENOSYS;
            }
        }
        else if (flags & IR0_O_EXCL)
        {
            return -EEXIST;
        }
    }
    else
    {
        int acc = 0;
        int accmode = flags & IR0_O_ACCMODE;
        stat_t st;

        if (!ops->stat || vfs_ops_stat(ops, path, &st) != 0)
            return -ENOENT;

        if (accmode == IR0_O_RDONLY || accmode == IR0_O_RDWR)
            acc |= 0x1;
        if (accmode == IR0_O_WRONLY || accmode == IR0_O_RDWR)
            acc |= 0x2;
        if (!ir0_check_file_access(path, acc))
            return -EACCES;
    }

    if ((flags & IR0_O_TRUNC) && ((flags & IR0_O_ACCMODE) != IR0_O_RDONLY))
    {
        stat_t st;

        if (!ops->stat || vfs_ops_stat(ops, path, &st) != 0)
            return -ENOENT;
        if (S_ISDIR(st.st_mode))
            return -EISDIR;
        if (S_ISREG(st.st_mode))
        {
            ret = vfs_truncate(path, 0);
            if (ret != 0)
                return ret;
            if (DEBUG_VFS)
                klog_info("VFS", "CLASSIFY VFS_TRUNCATE_GENERIC");
        }
    }

    struct vfs_file *f = vfs_file_alloc(path, flags);
    if (!f)
        return -ENOMEM;

    *out = f;
    return 0;
}

int vfs_read(struct vfs_file *f, char *buf, size_t count)
{
    off_t new_pos;
    if (!f)   return -EBADF;
    if (count == 0) return 0;
    if (!buf) return -EFAULT;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_WRONLY)
        return -EBADF;

    struct vfs_ops *ops = vfs_ops_for_path(f->path);
    if (!ops || !ops->read)
        return -ENOSYS;

    size_t done = 0;
    int ret = vfs_ops_read(ops, f->path, buf, count, &done, f->pos);
    if (vfs_exec_audit_active)
    {
        vfs_exec_audit_log("vfs_read", f->path, ret, f->pos, count, done, 0, -1);
    }
    if (ret != 0)
        return ret;
    if (done > count)
        return -EIO;
    if (ir0_off_add_size_overflow(f->pos, done, &new_pos))
        return -EOVERFLOW;
    f->pos = new_pos;
    return (int)done;
}

int vfs_write(struct vfs_file *f, const char *buf, size_t count)
{
    off_t new_pos;
    if (!f)   return -EBADF;
    if (count == 0) return 0;
    if (!buf) return -EFAULT;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_RDONLY)
        return -EBADF;
    if (vfs_mount_is_readonly(f->path))
        return -EROFS;

    struct vfs_ops *ops = vfs_ops_for_path(f->path);
    if (!ops || !ops->write)
        return -ENOSYS;

    /*
     * O_APPEND: move to end before every write.
     * We grab the current size via stat.
     */
    if (f->flags & IR0_O_APPEND) {
        stat_t st;
        if (ops->stat && vfs_ops_stat(ops, f->path, &st) == 0)
            f->pos = st.st_size;
    }

    size_t done = 0;
    int ret = vfs_ops_write(ops, f->path, buf, count, &done, f->pos);
    if (ret != 0)
        return ret;
    if (done > count)
        return -EIO;
    if (ir0_off_add_size_overflow(f->pos, done, &new_pos))
        return -EOVERFLOW;
    f->pos = new_pos;
    return (int)done;
}

int vfs_pread(struct vfs_file *f, char *buf, size_t count, off_t offset)
{
    if (!f)
        return -EBADF;
    if (count == 0)
        return 0;
    if (!buf)
        return -EFAULT;
    if (offset < 0)
        return -EINVAL;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_WRONLY)
        return -EBADF;

    struct vfs_ops *ops = vfs_ops_for_path(f->path);
    if (!ops || !ops->read)
        return -ENOSYS;

    size_t done = 0;
    int ret = vfs_ops_read(ops, f->path, buf, count, &done, offset);

    if (ret != 0)
        return ret;
    return (int)done;
}

int vfs_pwrite(struct vfs_file *f, const char *buf, size_t count, off_t offset)
{
    if (!f)
        return -EBADF;
    if (count == 0)
        return 0;
    if (!buf)
        return -EFAULT;
    if (offset < 0)
        return -EINVAL;
    if ((f->flags & IR0_O_ACCMODE) == IR0_O_RDONLY)
        return -EBADF;
    if (vfs_mount_is_readonly(f->path))
        return -EROFS;

    struct vfs_ops *ops = vfs_ops_for_path(f->path);
    if (!ops || !ops->write)
        return -ENOSYS;

    size_t done = 0;
    int ret = vfs_ops_write(ops, f->path, buf, count, &done, offset);

    if (ret != 0)
        return ret;
    return (int)done;
}

int vfs_close(struct vfs_file *f)
{
	return vfs_file_put_last(f, ir0_flock_release_file);
}

off_t vfs_lseek(struct vfs_file *f, off_t offset, int whence)
{
    if (!f)
        return -EBADF;

    off_t new_pos;
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        if (ir0_add_overflow(f->pos, offset, &new_pos))
            return -EOVERFLOW;
        break;
    case SEEK_END: {
        struct vfs_ops *ops = vfs_ops_for_path(f->path);
        if (!ops || !ops->stat)
            return -ENOSYS;
        stat_t st;
        if (vfs_ops_stat(ops, f->path, &st) != 0)
            return -EIO;
        if (ir0_add_overflow(st.st_size, offset, &new_pos))
            return -EOVERFLOW;
        break;
    }
    default:
        return -EINVAL;
    }

    if (new_pos < 0)
        return -EINVAL;
    f->pos = new_pos;
    return new_pos;
}

int vfs_read_file(const char *path, void **data, size_t *size)
{
    struct vfs_ops *ops;
    char backend[VFS_PATH_MAX];
    stat_t st;
    int ret;
    size_t fsize;
    void *buf;
    size_t total;

    if (!path || !data || !size || path[0] != '/')
    {
        if (vfs_exec_audit_active)
            vfs_exec_audit_log("read_file_bad_path", path, -EINVAL, 0, 0, 0, 0, -1);
        return -EINVAL;
    }

    ops = vfs_ops_for_path(path);
    if (!ops || !ops->read || !ops->stat)
    {
        if (vfs_exec_audit_active)
        {
            klog_print("[EXEC_AUDIT][VFS] stage=lookup_no_ops path=");
            klog_print(path);
            klog_print(" ops=");
            klog_hex64((uint64_t)(uintptr_t)ops);
            klog_print("\n");
            vfs_exec_audit_log("lookup_fail", path, -ENOSYS, 0, 0, 0, 0, -1);
        }
        return -ENOSYS;
    }

    if (vfs_exec_audit_active)
    {
        klog_print("[EXEC_AUDIT][VFS] stage=lookup_ok path=");
        klog_print(path);
        klog_print(" ops=");
        klog_hex64((uint64_t)(uintptr_t)ops);
        klog_print("\n");
    }

    ret = vfs_backend_path(ops, path, backend, sizeof(backend));
    if (ret != 0)
        return ret;
    vfs_fs_op_acquire();

    ret = ops->stat(backend, &st);
    if (ret != 0)
    {
        vfs_exec_audit_log("stat_fail", path, ret, 0, 0, 0, 0, -1);
        vfs_fs_op_release();
        return ret;
    }

    if (st.st_size < 0)
    {
        vfs_exec_audit_log("stat_bad_size", path, -EIO, 0, 0, 0,
                           (uint64_t)st.st_ino, st.st_size);
        vfs_fs_op_release();
        return -EIO;
    }

    fsize = (size_t)st.st_size;
    vfs_exec_audit_log("stat_ok", path, 0, 0, 0, 0,
                       (uint64_t)st.st_ino, st.st_size);

    if (fsize > VFS_READ_FILE_MAX_BYTES)
    {
        vfs_exec_audit_log("file_too_large", path, -EFBIG, 0, fsize, 0,
                           (uint64_t)st.st_ino, st.st_size);
        vfs_fs_op_release();
        return -EFBIG;
    }
    if (fsize == 0)
    {
        *data = NULL;
        *size = 0;
        vfs_exec_audit_log("file_size_zero", path, 0, 0, 0, 0,
                           (uint64_t)st.st_ino, 0);
        vfs_fs_op_release();
        return 0;
    }

    buf = kmalloc(fsize);
    if (!buf)
    {
        /* Audit is usually off; this path decides whether an -ENOMEM from
         * exec is the image buffer or something further down. */
        klog_notice_fmt("VFS", "read_file no mem size=%x\n",
                        (unsigned)fsize);
        vfs_exec_audit_log("kmalloc_fail", path, -ENOMEM, 0, fsize, 0,
                           (uint64_t)st.st_ino, st.st_size);
        vfs_fs_op_release();
        return -ENOMEM;
    }

    total = 0;

    while (total < fsize)
    {
        size_t chunk = 0;

        ret = ops->read(backend, (uint8_t *)buf + total, fsize - total,
                        &chunk, (off_t)total);
        if (ret != 0)
        {
            vfs_exec_audit_log("read_err", path, ret, (off_t)total,
                               fsize - total, total,
                               (uint64_t)st.st_ino, st.st_size);
            exec_read_trace_vfs_read_file(path, (uint64_t)st.st_ino,
                                          st.st_size, (off_t)total,
                                          fsize - total, ret);
            kfree(buf);
            vfs_fs_op_release();
            return ret;
        }
        if (chunk == 0)
        {
            vfs_exec_audit_log("read_zero", path, -EIO, (off_t)total,
                               fsize - total, total,
                               (uint64_t)st.st_ino, st.st_size);
            kfree(buf);
            vfs_fs_op_release();
            return -EIO;
        }
        vfs_exec_audit_log("read_chunk", path, 0, (off_t)total,
                           fsize - total, chunk,
                           (uint64_t)st.st_ino, st.st_size);
        total += chunk;
    }

    if (total < fsize)
    {
        vfs_exec_audit_log("read_short", path, -EIO, (off_t)total,
                           fsize, total,
                           (uint64_t)st.st_ino, st.st_size);
        kfree(buf);
        vfs_fs_op_release();
        return -EIO;
    }

    *data = buf;
    *size = total;
    vfs_exec_audit_log("read_ok", path, 0, 0, fsize, total,
                       (uint64_t)st.st_ino, st.st_size);
    vfs_fs_op_release();
    return 0;
}
