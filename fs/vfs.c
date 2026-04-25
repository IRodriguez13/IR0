/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: vfs.c
 * Description: Minimal path-based VFS — mount table + ops dispatch.
 *
 * Every public operation follows the same pattern:
 *   1. validate path
 *   2. find the longest-matching mount point
 *   3. call mount->fs->ops->xxx(path, ...)
 */

#include "vfs.h"
#if CONFIG_ENABLE_FS_MINIX
#include "minix_fs.h"
#endif
#if CONFIG_ENABLE_FS_TMPFS
#include "tmpfs.h"
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
#include "simplefs.h"
#endif
#if CONFIG_ENABLE_FS_FAT16
#include "fat16_fs.h"
#endif
#include <ir0/path.h>
#include <ir0/logging.h>
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/permissions.h>
#include <kernel/process.h>
#include <ir0/block_dev.h>
#include <ir0/vga.h>
#include <config.h>
#include <string.h>

#define MAX_PATH 256
#define VFS_READ_FILE_MAX_BYTES (64U * 1024U * 1024U)

/* ------------------------------------------------------------------ */
/*  Global state                                                       */
/* ------------------------------------------------------------------ */
static struct vfs_fstype *fs_types  = NULL;
static struct vfs_mount  *mounts   = NULL;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static int validate_path(const char *path)
{
    if (!path)
        return -EINVAL;
    size_t len = strlen(path);
    if (len == 0)
        return -EINVAL;
    if (len >= MAX_PATH)
        return -ENAMETOOLONG;

    int comp = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            comp = 0;
        else if (++comp > 255)
            return -ENAMETOOLONG;

        if (*p < 0x20 || *p == 0x7F ||
            *p == '\\' || *p == '|' || *p == '<' || *p == '>')
            return -EINVAL;
    }
    return 0;
}

/**
 * Find the mount whose path is the longest prefix of @path.
 */
static struct vfs_mount *find_mount(const char *path)
{
    struct vfs_mount *best = NULL;
    size_t best_len = 0;
    size_t plen = strlen(path);

    for (struct vfs_mount *m = mounts; m; m = m->next) {
        size_t mlen = strlen(m->path);
        if (mlen > plen)
            continue;
        if (strncmp(path, m->path, mlen) != 0)
            continue;
        if (mlen < plen && m->path[mlen - 1] != '/' && path[mlen] != '/')
            continue;
        if (mlen > best_len) {
            best_len = mlen;
            best = m;
        }
    }
    return best;
}

/**
 * Return the vfs_ops for the filesystem that owns @path, or NULL.
 */
static struct vfs_ops *ops_for_path(const char *path)
{
    struct vfs_mount *m = find_mount(path);
    if (m && m->fs && m->fs->ops)
        return m->fs->ops;
    return NULL;
}

/**
 * Check that the current process has execute on every directory component.
 */
static int check_dir_traverse(const char *path)
{
    if (!current_process)
        return -EINVAL;
    if (is_root(current_process))
        return 0;
    if (strcmp(path, "/") == 0)
        return 0;

    char dir[MAX_PATH];
    strncpy(dir, "/", sizeof(dir));
    const char *p = path + 1;

    while (*p) {
        const char *slash = strchr(p, '/');
        if (!slash)
            break;
        size_t clen = (size_t)(slash - p);
        size_t dlen = strlen(dir);
        if (dlen + 1 + clen >= sizeof(dir))
            return -ENAMETOOLONG;
        if (dlen > 1)
            strncat(dir, "/", sizeof(dir) - strlen(dir) - 1);
        strncat(dir, p, clen);
        dir[sizeof(dir) - 1] = '\0';

        if (!check_file_access(dir, ACCESS_EXEC, current_process))
            return -EACCES;
        p = slash + 1;
    }
    return 0;
}

/**
 * Convenience: extract parent directory of @path into @out.
 * Returns 0 on success.
 */
static int parent_dir(const char *path, char *out, size_t out_sz)
{
    const char *last = strrchr(path, '/');
    if (!last || last == path) {
        strncpy(out, "/", out_sz);
        out[out_sz - 1] = '\0';
        return 0;
    }
    size_t len = (size_t)(last - path);
    if (len >= out_sz)
        return -ENAMETOOLONG;
    strncpy(out, path, len);
    out[len] = '\0';
    return 0;
}

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

static void normalize_mount_path(const char *in, char *out, size_t out_sz)
{
    if (!in || !out || out_sz == 0)
        return;

    strncpy(out, in, out_sz - 1);
    out[out_sz - 1] = '\0';

    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/')
    {
        out[len - 1] = '\0';
        len--;
    }
}

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

int vfs_register_fs(struct vfs_fstype *fs)
{
    if (!fs || !fs->name || strlen(fs->name) == 0)
        return -EINVAL;

    for (struct vfs_fstype *t = fs_types; t; t = t->next)
        if (strcmp(t->name, fs->name) == 0)
            return -EEXIST;

    fs->next = fs_types;
    fs_types = fs;
    return 0;
}

int vfs_init(void)
{
    fs_types = NULL;
    mounts   = NULL;
#if CONFIG_ENABLE_FS_MINIX
    minix_fs_register();
#endif
#if CONFIG_ENABLE_FS_TMPFS
    tmpfs_register();
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
    simplefs_register();
#endif
#if CONFIG_ENABLE_FS_FAT16
    fat16_fs_register();
#endif
    return 0;
}

struct vfs_mount *vfs_get_mounts(void)
{
    return mounts;
}

int vfs_mount(const char *dev, const char *path, const char *fstype)
{
    char mount_path[MAX_PATH];

    if (!fstype || !path)
        return -EINVAL;
    if (current_process && current_process->euid != ROOT_UID)
        return -EPERM;

    int ret = validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));

    for (struct vfs_mount *existing = mounts; existing; existing = existing->next)
    {
        if (strcmp(existing->path, mount_path) == 0)
            return -EBUSY;
    }

    struct vfs_fstype *ft = NULL;
    for (struct vfs_fstype *t = fs_types; t; t = t->next) {
        if (strcmp(t->name, fstype) == 0) {
            ft = t;
            break;
        }
    }
    if (!ft)
        return -ENODEV;

    struct vfs_mount *m = kmalloc(sizeof(*m));
    if (!m)
        return -ENOMEM;

    strncpy(m->path, mount_path, sizeof(m->path) - 1);
    m->path[sizeof(m->path) - 1] = '\0';
    if (dev) {
        strncpy(m->dev, dev, sizeof(m->dev) - 1);
        m->dev[sizeof(m->dev) - 1] = '\0';
    } else {
        m->dev[0] = '\0';
    }
    m->fs = ft;
    m->next = mounts;

    ret = ft->mount(dev, mount_path);
    if (ret != 0)
    {
        kfree(m);
        return ret;
    }

    mounts = m;
    return 0;
}

int vfs_init_root(void)
{
    const char *root_blk = CONFIG_ROOT_BLOCK_DEVICE;
    const char *root_fs = CONFIG_ROOT_FILESYSTEM;
    char root_dev_path[64];
    int can_use_block_dev = 1;

    print("VFS: Initializing VFS...\n");
    int ret = vfs_init();
    if (ret != 0) {
        print("VFS: ERROR - vfs_init failed\n");
        return ret;
    }
    print("VFS: vfs_init OK (builtin filesystems registered)\n");

    snprintf(root_dev_path, sizeof(root_dev_path), "/dev/%s", root_blk);

#if !CONFIG_ENABLE_FS_MINIX
    if (strcmp(root_fs, "minix") == 0)
        can_use_block_dev = 0;
#endif

    if (can_use_block_dev && !block_dev_is_present(root_blk)) {
        print("VFS: ERROR - No block device configured for root available\n");
        return -ENODEV;
    }

    print("VFS: Mounting root filesystem...\n");
    ret = vfs_mount(root_dev_path, "/", root_fs);
    if (ret != 0) {
        print("VFS: configured root mount failed, falling back to tmpfs root\n");
#if CONFIG_ENABLE_FS_TMPFS
        ret = vfs_mount("none", "/", "tmpfs");
        if (ret != 0) {
            print("VFS: ERROR - tmpfs fallback also failed\n");
            return ret;
        }
        print("VFS: tmpfs root mounted (fallback)\n");
#else
        print("VFS: ERROR - tmpfs fallback disabled by configuration\n");
        return ret;
#endif
    } else {
        print("VFS: vfs_mount OK (configured root)\n");
    }
    return 0;
}

int vfs_init_with_minix(void)
{
    return vfs_init_root();
}

/* ------------------------------------------------------------------ */
/*  File operations                                                    */
/* ------------------------------------------------------------------ */

int vfs_open(const char *path, int flags, mode_t mode, struct vfs_file **out)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!out)     return -EFAULT;
    if (!current_process) return -ESRCH;

    ret = check_dir_traverse(path);
    if (ret != 0)
        return ret;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops)
        return -ENODEV;

    if (flags & O_CREAT) {
        char parent[MAX_PATH];
        if (parent_dir(path, parent, sizeof(parent)) == 0)
            if (!check_file_access(parent, 0x2, current_process))
                return -EACCES;

        stat_t st;
        if (ops->stat && ops->stat(path, &st) != 0) {
            if (ops->create) {
                mode_t fmode = mode ? (mode & 0777) : 0644;
                ret = ops->create(path, fmode);
                if (ret != 0)
                    return ret;
            } else {
                return -ENOSYS;
            }
        } else if (flags & O_EXCL) {
            return -EEXIST;
        }
    } else {
        int acc = 0;
        int accmode = flags & O_ACCMODE;
        if (accmode == O_RDONLY || accmode == O_RDWR) acc |= 0x1;
        if (accmode == O_WRONLY || accmode == O_RDWR) acc |= 0x2;
        if (!check_file_access(path, acc, current_process))
            return -EACCES;

        stat_t st;
        if (!ops->stat || ops->stat(path, &st) != 0)
            return -ENOENT;
    }

    if ((flags & O_TRUNC) && ((flags & O_ACCMODE) != O_RDONLY))
    {
        if (!ops->truncate)
            return -ENOSYS;
        ret = ops->truncate(path, 0);
        if (ret == -1)
            return -EIO;
        if (ret != 0)
            return ret;
    }

    struct vfs_file *f = kmalloc_try(sizeof(*f));
    if (!f)
        return -ENOMEM;

    strncpy(f->path, path, sizeof(f->path) - 1);
    f->path[sizeof(f->path) - 1] = '\0';
    f->pos   = 0;
    f->flags = flags;
    f->ref_count = 1;
    *out = f;
    return 0;
}

int vfs_read(struct vfs_file *f, char *buf, size_t count)
{
    if (!f)   return -EBADF;
    if (!buf) return -EFAULT;
    if (count == 0) return 0;
    if ((f->flags & O_ACCMODE) == O_WRONLY)
        return -EBADF;

    struct vfs_ops *ops = ops_for_path(f->path);
    if (!ops || !ops->read)
        return -ENOSYS;

    size_t done = 0;
    int ret = ops->read(f->path, buf, count, &done, f->pos);
    if (ret != 0)
        return ret;
    f->pos += (off_t)done;
    return (int)done;
}

int vfs_write(struct vfs_file *f, const char *buf, size_t count)
{
    if (!f)   return -EBADF;
    if (!buf) return -EFAULT;
    if (count == 0) return 0;
    if ((f->flags & O_ACCMODE) == O_RDONLY)
        return -EBADF;

    struct vfs_ops *ops = ops_for_path(f->path);
    if (!ops || !ops->write)
        return -ENOSYS;

    /*
     * O_APPEND: move to end before every write.
     * We grab the current size via stat.
     */
    if (f->flags & O_APPEND) {
        stat_t st;
        if (ops->stat && ops->stat(f->path, &st) == 0)
            f->pos = st.st_size;
    }

    size_t done = 0;
    int ret = ops->write(f->path, buf, count, &done, f->pos);
    if (ret != 0)
        return ret;
    f->pos += (off_t)done;
    return (int)done;
}

int vfs_close(struct vfs_file *f)
{
    if (!f)
        return -EBADF;
    if (f->ref_count > 0)
        f->ref_count--;
    if (f->ref_count <= 0)
        kfree(f);
    return 0;
}

void vfs_file_acquire(struct vfs_file *f)
{
    if (!f)
        return;
    f->ref_count++;
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
        new_pos = f->pos + offset;
        break;
    case SEEK_END: {
        struct vfs_ops *ops = ops_for_path(f->path);
        if (!ops || !ops->stat)
            return -ENOSYS;
        stat_t st;
        if (ops->stat(f->path, &st) != 0)
            return -EIO;
        new_pos = st.st_size + offset;
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

/* ------------------------------------------------------------------ */
/*  Path operations                                                    */
/* ------------------------------------------------------------------ */

int vfs_stat(const char *path, stat_t *buf)
{
    if (!path || !buf)
        return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0) return ret;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->stat)
        return -ENODEV;
    return ops->stat(path, buf);
}

int vfs_mkdir(const char *path, int mode)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!current_process) return -ESRCH;

    char parent[MAX_PATH];
    if (parent_dir(path, parent, sizeof(parent)) == 0)
        if (strcmp(parent, "/") != 0)
            if (!check_file_access(parent, 0x2, current_process))
                return -EACCES;

    if (strcmp(path, "/") == 0)
        return -EEXIST;

    stat_t st;
    if (vfs_stat(path, &st) == 0 && (st.st_mode & S_IFMT) != 0)
        return -EEXIST;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->mkdir)
        return -ENOSYS;
    return ops->mkdir(path, (mode_t)mode);
}

int vfs_unlink(const char *path)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!current_process) return -ESRCH;
    if (strcmp(path, "/") == 0) return -EPERM;

    stat_t st;
    if (vfs_stat(path, &st) != 0)
        return -ENOENT;

    if (S_ISDIR(st.st_mode)) {
        char parent[MAX_PATH];
        if (parent_dir(path, parent, sizeof(parent)) == 0)
            if (!check_file_access(parent, 0x2, current_process))
                return -EACCES;
    } else {
        if (!check_file_access(path, 0x2, current_process))
            return -EACCES;
    }

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->unlink)
        return -ENOSYS;
    return ops->unlink(path);
}

int vfs_link(const char *oldpath, const char *newpath)
{
    int ret = validate_path(oldpath);
    if (ret != 0) return ret;
    ret = validate_path(newpath);
    if (ret != 0) return ret;
    if (!current_process) return -ESRCH;

    stat_t st;
    if (vfs_stat(oldpath, &st) != 0)
        return -ENOENT;
    if (!check_file_access(oldpath, 0x1, current_process))
        return -EACCES;

    char parent[MAX_PATH];
    if (parent_dir(newpath, parent, sizeof(parent)) == 0)
        if (!check_file_access(parent, 0x2, current_process))
            return -EACCES;

    if (vfs_stat(newpath, &st) == 0)
        return -EEXIST;

    struct vfs_ops *ops = ops_for_path(oldpath);
    if (!ops || !ops->link)
        return -ENOSYS;
    return ops->link(oldpath, newpath);
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    int ret = validate_path(oldpath);
    if (ret != 0) return ret;
    ret = validate_path(newpath);
    if (ret != 0) return ret;
    if (!current_process) return -ESRCH;

    stat_t st_old;
    if (vfs_stat(oldpath, &st_old) != 0)
        return -ENOENT;
    if (S_ISDIR(st_old.st_mode))
        return -EISDIR;
    if (!check_file_access(oldpath, ACCESS_READ | ACCESS_WRITE, current_process))
        return -EACCES;

    char parent[MAX_PATH];
    if (parent_dir(newpath, parent, sizeof(parent)) == 0)
        if (!check_file_access(parent, ACCESS_WRITE, current_process))
            return -EACCES;

    struct vfs_mount *m_old = find_mount(oldpath);
    struct vfs_mount *m_new = find_mount(newpath);
    if (m_old != m_new)
        return -EXDEV;

    stat_t st_new;
    if (vfs_stat(newpath, &st_new) == 0) {
        if (S_ISDIR(st_new.st_mode))
            return -EISDIR;
        if (vfs_unlink(newpath) != 0)
            return -EACCES;
    }

    struct vfs_ops *ops = ops_for_path(oldpath);
    if (!ops || !ops->link)
        return -ENOSYS;
    ret = ops->link(oldpath, newpath);
    if (ret != 0)
        return ret;
    return vfs_unlink(oldpath);
}

int vfs_rmdir(const char *path)
{
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!current_process)
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

    ret = check_dir_traverse(norm);
    if (ret != 0)
        return ret;

    char parent[MAX_PATH];
    if (parent_dir(norm, parent, sizeof(parent)) == 0)
    {
        if (!check_file_access(parent, ACCESS_WRITE | ACCESS_EXEC, current_process))
            return -EACCES;
    }

    struct vfs_ops *ops = ops_for_path(norm);
    if (!ops || !ops->rmdir)
        return -ENOSYS;
    return ops->rmdir(norm);
}

static int rmdir_recursive_impl(const char *path, int depth)
{
    if (depth > 32)
        return -ELOOP;

    int ret = validate_path(path);
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

    struct vfs_dirent entries[32];
    int n = vfs_readdir(norm, entries, 32);
    if (n < 0) {
        struct vfs_ops *ops = ops_for_path(norm);
        if (ops && ops->rmdir)
            return ops->rmdir(norm);
        return n;
    }

    size_t nlen = strlen(norm);
    for (int i = 0; i < n; i++) {
        if (entries[i].name[0] == '\0')
            continue;
        if (entries[i].name[0] == '.' &&
            (entries[i].name[1] == '\0' ||
             (entries[i].name[1] == '.' && entries[i].name[2] == '\0')))
            continue;

        char full[MAX_PATH];
        if (build_path(full, sizeof(full), norm, entries[i].name) != 0)
            continue;
        char resolved[MAX_PATH];
        if (normalize_path(full, resolved, sizeof(resolved)) != 0)
            continue;
        if (resolved[0] == '/' && resolved[1] == '\0')
            continue;
        if (strcmp(resolved, norm) == 0)
            continue;

        size_t rlen = strlen(resolved);
        if (rlen < nlen)
            continue;
        if (rlen <= nlen || strncmp(resolved, norm, nlen) != 0 || resolved[nlen] != '/')
            continue;

        stat_t est;
        if (vfs_stat(resolved, &est) == 0) {
            if (S_ISDIR(est.st_mode)) {
                int rc = rmdir_recursive_impl(resolved, depth + 1);
                if (rc != 0)
                    return rc;
            } else {
                int rc = vfs_unlink(resolved);
                if (rc != 0)
                    return rc;
            }
        }
    }

    struct vfs_ops *ops = ops_for_path(norm);
    if (ops && ops->rmdir)
        return ops->rmdir(norm);
    return -ENOSYS;
}

int vfs_rmdir_recursive(const char *path)
{
    return rmdir_recursive_impl(path, 0);
}

int vfs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
    if (!path || !entries || max <= 0)
        return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!current_process) return -ESRCH;

    if (!check_file_access(path, ACCESS_EXEC, current_process))
        return -EACCES;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->readdir)
        return -ENOSYS;
    return ops->readdir(path, entries, max);
}

int vfs_chown(const char *path, uid_t owner, gid_t group)
{
    if (!path) return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0) return ret;
    if (!current_process)
        return -ESRCH;
    if (current_process->euid != ROOT_UID)
        return -EPERM;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->chown)
        return -ENOSYS;
    return ops->chown(path, owner, group);
}

int vfs_chmod(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    if (!current_process)
        return -ESRCH;

    stat_t st;
    ret = vfs_stat(path, &st);
    if (ret != 0)
        return ret;

    if (current_process->euid != ROOT_UID && current_process->euid != st.st_uid)
        return -EPERM;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->chmod)
        return -ENOSYS;
    return ops->chmod(path, mode);
}

int vfs_truncate(const char *path, size_t length)
{
    if (!path)
        return -EINVAL;
    if (!current_process)
        return -ESRCH;
    int ret = validate_path(path);
    if (ret != 0)
        return ret;
    if (!check_file_access(path, ACCESS_WRITE, current_process))
        return -EACCES;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->truncate)
        return -ENOSYS;
    return ops->truncate(path, length);
}


int vfs_read_file(const char *path, void **data, size_t *size)
{
    if (!path || !data || !size || path[0] != '/')
        return -EINVAL;

    struct vfs_ops *ops = ops_for_path(path);
    if (!ops || !ops->read || !ops->stat)
        return -ENOSYS;

    stat_t st;
    int ret = ops->stat(path, &st);
    if (ret != 0)
        return ret;

    if (st.st_size < 0)
        return -EIO;

    size_t fsize = (size_t)st.st_size;
    if (fsize > VFS_READ_FILE_MAX_BYTES)
        return -EFBIG;
    if (fsize == 0) {
        *data = NULL;
        *size = 0;
        return 0;
    }

    void *buf = kmalloc_try(fsize);
    if (!buf)
        return -ENOMEM;

    size_t total = 0;

    while (total < fsize) {
        size_t chunk = 0;
        ret = ops->read(path, (uint8_t *)buf + total, fsize - total,
                        &chunk, (off_t)total);

        if (ret != 0) {
            kfree(buf);
            return ret;
        }
        if (chunk == 0) {
            kfree(buf);
            return -EIO;
        }
        total += chunk;
    }

    *data = buf;
    *size = total;
    return 0;
}
