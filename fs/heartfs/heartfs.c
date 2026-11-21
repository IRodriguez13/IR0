/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: heartfs.c
 * Description: heartfs — the /heart filesystem: IR0 kernel meta, /heart/src
 *              and read views of /proc and /sys
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fs/vfs.h>
#include <ir0/cred.h>
#include <ir0/errno.h>
#include <ir0/open_flags.h>
#include <ir0/procfs.h>
#include <ir0/pseudo_fs.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <ir0/version.h>
#include <string.h>

/* Generated at build time by scripts/gen_heart_src_blob.py */
#include <generated/heart_src_blob.h>

#define HEART_ROOT "/heart"
#define HEART_ROOT_LEN (sizeof(HEART_ROOT) - 1)
#define HEART_DENNIS HEART_ROOT "/dennis"
#define HEART_SRC_DIR "src"
#define HEART_SRC_DIR_LEN (sizeof(HEART_SRC_DIR) - 1)

static const char heart_readme_text[] =
    "IR0 /heart — unified read-only view of kernel identity and pseudo-fs.\n"
    "Does not replace /proc or /sys; those mounts remain authoritative.\n"
    "Layout: README, MANIFESTO, proc/, sys/, kernel/, src/, dennis/\n"
    "Start with: cat /heart/MANIFESTO\n"
    "Sources to edit: /heart/dennis/src (mount -t 9p dennis /heart/dennis/src)\n";

/*
 * Brief identity for guests (cat /heart/MANIFESTO). Keep short — serial TTY.
 */
static const char heart_manifesto_text[] =
    "IR0 — Independent Research Operating system\n"
    "\n"
    "A Unix-like research kernel with a Linux-compatible userspace ABI\n"
    "goal: unmodified Linux userspace and simple desktop clients on x86-64\n"
    "(ARM64"
    "bring-up in progress).\n"
    "\n"
    "What you are running:\n"
    "  - Kernel: IR0 (syscalls, VFS, MM, sched, /proc /sys /dev /heart)\n"
    "  - Userspace: ISD (IR0 Software Distribution)\n"
    "  - Philosophy: honest surfaces, no fake success, Linux as ground\n"
    "    truth for ABI contracts when claimed.\n"
    "\n"
    "Explore:\n"
    "  cat /heart/MANIFESTO   this text\n"
    "  cat /heart/README      /heart layout\n"
    "  cat /heart/kernel/version\n"
    "  man IR0-boot           guest manual pages (cat7)\n"
    "  ls /usr/ken/games      Ken Thompson nod — small games\n"
    "  ls /heart/dennis/src   Dennis Ritchie nod — IR0 sources (9p/writable)\n"
    "\n"
    "Home: Documentation/ in the source tree; SETUP.md for QEMU.\n";

static char g_heart_version_buf[128];
static char g_heart_build_buf[256];
static char g_heart_features_buf[256];

/* Files with fixed text, by path below /heart, in listing order. */
static const struct
{
    const char *rel;
    const char *text;
} heart_text_files[] = {
    { "README", heart_readme_text },
    { "MANIFESTO", heart_manifesto_text },
    { "kernel/version", g_heart_version_buf },
    { "kernel/build", g_heart_build_buf },
    { "kernel/features", g_heart_features_buf },
};

#define HEART_TEXT_FILE_COUNT \
    (sizeof(heart_text_files) / sizeof(heart_text_files[0]))

/* Entries of /heart itself, in listing order. */
static const struct
{
    const char *name;
    unsigned char type;
} heart_root_entries[] = {
    { "README", DT_REG },
    { "MANIFESTO", DT_REG },
    { "proc", DT_DIR },
    { "sys", DT_DIR },
    { "kernel", DT_DIR },
    { "src", DT_DIR },
    { "dennis", DT_DIR },
};

static void heart_fill_meta_buffers(void)
{
    if (g_heart_version_buf[0] != '\0')
        return;
    snprintf(g_heart_version_buf, sizeof(g_heart_version_buf), "%s\n",
             IR0_VERSION_STRING);
    snprintf(g_heart_build_buf, sizeof(g_heart_build_buf),
             "%s %s by %s@%s with %s\n", IR0_BUILD_DATE, IR0_BUILD_TIME,
             IR0_BUILD_USER, IR0_BUILD_HOST, IR0_BUILD_CC);
    snprintf(g_heart_features_buf, sizeof(g_heart_features_buf),
             "pseudo_fs_registry\nheart_facade\nheart_src\nfd_table_pseudo\n");
}

/*
 * Copy the path below /heart of @path into @rel, without trailing slashes
 * ("" for /heart itself). -ENOENT if @path is not under /heart.
 */
static int heart_rel_path(const char *path, char *rel, size_t rel_sz)
{
    size_t len;

    if (!path || strncmp(path, HEART_ROOT, HEART_ROOT_LEN) != 0)
        return -ENOENT;
    path += HEART_ROOT_LEN;
    if (path[0] != '\0' && path[0] != '/')
        return -ENOENT;
    while (path[0] == '/')
        path++;
    len = strlen(path);
    while (len > 0 && path[len - 1] == '/')
        len--;
    if (len >= rel_sz)
        return -ENAMETOOLONG;
    memcpy(rel, path, len);
    rel[len] = '\0';
    return 0;
}

/* /heart/proc/X → /proc/X and /heart/sys/X → /sys/X; 0 if @rel is neither. */
static int heart_alias_canonical(const char *rel, char *out, size_t out_sz)
{
    if (strncmp(rel, "proc/", 5) == 0 && rel[5] != '\0')
    {
        snprintf(out, out_sz, "/proc/%s", rel + 5);
        return 1;
    }
    if (strncmp(rel, "sys/", 4) == 0 && rel[4] != '\0')
    {
        snprintf(out, out_sz, "/sys/%s", rel + 4);
        return 1;
    }
    return 0;
}

/* The /heart/src blob file at @rel ("src/<path>"), or NULL. */
static const heart_src_file_t *heart_src_find(const char *rel)
{
    if (strncmp(rel, HEART_SRC_DIR "/", HEART_SRC_DIR_LEN + 1) != 0)
        return NULL;
    rel += HEART_SRC_DIR_LEN + 1;
    for (unsigned i = 0; i < HEART_SRC_FILE_COUNT; i++)
    {
        if (strcmp(heart_src_files[i].rel_path, rel) == 0)
            return &heart_src_files[i];
    }
    return NULL;
}

/* The bytes of the file at @rel; -ENOENT if @rel is not a heartfs file. */
static int heart_file_data(const char *rel, const char **data, size_t *size)
{
    const heart_src_file_t *src;

    heart_fill_meta_buffers();
    for (size_t i = 0; i < HEART_TEXT_FILE_COUNT; i++)
    {
        if (strcmp(heart_text_files[i].rel, rel) == 0)
        {
            *data = heart_text_files[i].text;
            *size = strlen(heart_text_files[i].text);
            return 0;
        }
    }
    src = heart_src_find(rel);
    if (!src)
        return -ENOENT;
    *data = (const char *)src->data;
    *size = src->size;
    return 0;
}

#define HEART_FILE_COUNT (HEART_TEXT_FILE_COUNT + HEART_SRC_FILE_COUNT)

/*
 * The rest of the path of file @idx below the directory @dir (@len bytes),
 * or NULL if the file is not below it. Files are numbered text files
 * first, then the /heart/src blob, which fixes the listing order.
 */
static const char *heart_file_below(unsigned idx, const char *dir, size_t len)
{
    const char *rel;

    if (idx < HEART_TEXT_FILE_COUNT)
    {
        rel = heart_text_files[idx].rel;
    }
    else
    {
        if (strncmp(dir, HEART_SRC_DIR, HEART_SRC_DIR_LEN) != 0)
            return NULL;
        rel = heart_src_files[idx - HEART_TEXT_FILE_COUNT].rel_path;
        if (len == HEART_SRC_DIR_LEN)
            return rel;
        if (dir[HEART_SRC_DIR_LEN] != '/')
            return NULL;
        dir += HEART_SRC_DIR_LEN + 1;
        len -= HEART_SRC_DIR_LEN + 1;
    }
    if (strncmp(rel, dir, len) != 0 || rel[len] != '/')
        return NULL;
    return rel + len + 1;
}

/* 1 if some heartfs file lives below the directory @dir. */
static int heart_has_children(const char *dir)
{
    size_t len = strlen(dir);

    for (unsigned i = 0; i < HEART_FILE_COUNT; i++)
    {
        if (heart_file_below(i, dir, len))
            return 1;
    }
    return 0;
}

static int heart_is_dir(const char *rel)
{
    if (rel[0] == '\0' || strcmp(rel, "proc") == 0 ||
        strcmp(rel, "sys") == 0 || strcmp(rel, "dennis") == 0)
        return 1;
    return heart_has_children(rel);
}

static void heart_fill_dir_stat(stat_t *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    st->st_nlink = 2;
    pseudo_fs_stat_now(st);
}

static int heart_alias_stat(const char *canon, stat_t *st)
{
    const pseudo_fs_entry_t *pf;

    pseudo_fs_nodes_register_all();
    pf = pseudo_fs_lookup(canon);
    if (pf && pf->ops && pf->ops->stat)
        return pf->ops->stat(pf->ctx, st);
    return pseudo_fs_stat_path(canon, st);
}

static int heart_stat(const char *path, stat_t *st)
{
    char rel[VFS_PATH_MAX];
    char canon[VFS_PATH_MAX];
    const char *data;
    size_t size;
    int ret;

    ret = heart_rel_path(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    if (heart_is_dir(rel))
    {
        heart_fill_dir_stat(st);
        return 0;
    }
    if (heart_alias_canonical(rel, canon, sizeof(canon)))
        return heart_alias_stat(canon, st);

    ret = heart_file_data(rel, &data, &size);
    if (ret != 0)
        return ret;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = (off_t)size;
    pseudo_fs_stat_now(st);
    return 0;
}

static int heart_add_dirent(struct vfs_dirent *entries, int max, int n,
                            const char *name, size_t len, unsigned char type)
{
    if (n >= max || len == 0 || len >= sizeof(entries[n].name))
        return n;
    for (int i = 0; i < n; i++)
    {
        if (strncmp(entries[i].name, name, len) == 0 &&
            entries[i].name[len] == '\0')
            return n;
    }
    memcpy(entries[n].name, name, len);
    entries[n].name[len] = '\0';
    entries[n].type = type;
    return n + 1;
}

static int heart_readdir(const char *path, struct vfs_dirent *entries, int max)
{
    char rel[VFS_PATH_MAX];
    size_t len;
    int n = 0;
    int ret;

    ret = heart_rel_path(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;

    if (rel[0] == '\0')
    {
        for (size_t i = 0;
             i < sizeof(heart_root_entries) / sizeof(heart_root_entries[0]); i++)
            n = heart_add_dirent(entries, max, n, heart_root_entries[i].name,
                                 strlen(heart_root_entries[i].name),
                                 heart_root_entries[i].type);
        return n;
    }
    if (strcmp(rel, "proc") == 0)
    {
        pseudo_fs_nodes_register_all();
        return pseudo_fs_collect_registry_children("/proc", entries, max, 0);
    }
    if (strcmp(rel, "sys") == 0)
    {
        pseudo_fs_nodes_register_all();
        return pseudo_fs_collect_registry_children("/sys", entries, max, 0);
    }
    if (strcmp(rel, "dennis") == 0)
        return 0;
    if (!heart_has_children(rel))
        return -ENOTDIR;

    len = strlen(rel);
    for (unsigned i = 0; i < HEART_FILE_COUNT; i++)
    {
        const char *name = heart_file_below(i, rel, len);
        const char *slash;

        if (!name)
            continue;
        slash = strchr(name, '/');
        n = heart_add_dirent(entries, max, n, name,
                             slash ? (size_t)(slash - name) : strlen(name),
                             slash ? DT_DIR : DT_REG);
    }
    return n;
}

/*
 * The /proc|/sys node behind an alias path, acquired for one operation;
 * release it with pseudo_fs_release_ops().
 */
static int heart_alias_acquire(const char *canon, int flags,
                               const pseudo_fs_ops_t **ops, void **ctx,
                               int *dynamic)
{
    pseudo_fs_nodes_register_all();
    return (int)pseudo_fs_acquire_path(canon, flags, ops, ctx, dynamic);
}

static int heart_open(const char *path, int flags)
{
    char rel[VFS_PATH_MAX];
    char canon[VFS_PATH_MAX];
    const struct ir0_task_cred *cred;
    const pseudo_fs_ops_t *ops;
    void *ctx;
    int dynamic;
    int ret;

    ret = heart_rel_path(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    if (!heart_alias_canonical(rel, canon, sizeof(canon)))
        return 0;

    cred = ir0_current_cred();
    if (cred && is_proc_path(canon))
    {
        ret = proc_access_path(canon, cred->euid);
        if (ret != 0)
            return ret;
    }
    ret = heart_alias_acquire(canon, flags, &ops, &ctx, &dynamic);
    if (ret != 0)
        return ret;
    return (int)pseudo_fs_release_ops(ops, ctx, dynamic);
}

static int heart_read(const char *path, void *buf, size_t count,
                      size_t *bytes_read, off_t offset)
{
    char rel[VFS_PATH_MAX];
    char canon[VFS_PATH_MAX];
    const char *data;
    size_t size;
    int ret;

    *bytes_read = 0;
    if (offset < 0)
        return -EINVAL;
    ret = heart_rel_path(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    if (heart_is_dir(rel))
        return -EISDIR;

    if (heart_alias_canonical(rel, canon, sizeof(canon)))
    {
        const pseudo_fs_ops_t *ops;
        void *ctx;
        int dynamic;
        int64_t n;

        ret = heart_alias_acquire(canon, IR0_O_RDONLY, &ops, &ctx, &dynamic);
        if (ret != 0)
            return ret;
        n = pseudo_fs_ops_read(ops, ctx, buf, count, &offset);
        (void)pseudo_fs_release_ops(ops, ctx, dynamic);
        if (n < 0)
            return (int)n;
        *bytes_read = (size_t)n;
        return 0;
    }

    ret = heart_file_data(rel, &data, &size);
    if (ret != 0)
        return ret;
    if ((size_t)offset >= size)
        return 0;
    size -= (size_t)offset;
    *bytes_read = size < count ? size : count;
    memcpy(buf, data + offset, *bytes_read);
    return 0;
}

/*
 * Only the /proc|/sys nodes behind an alias take writes, without an
 * offset; heartfs files have no write method, which Linux reports as
 * EINVAL.
 */
static int heart_write(const char *path, const void *buf, size_t count,
                       size_t *bytes_written, off_t offset)
{
    char rel[VFS_PATH_MAX];
    char canon[VFS_PATH_MAX];
    const pseudo_fs_ops_t *ops;
    void *ctx;
    int dynamic;
    int64_t n;
    int ret;

    (void)offset;
    *bytes_written = 0;
    ret = heart_rel_path(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    if (!heart_alias_canonical(rel, canon, sizeof(canon)))
        return -EINVAL;

    ret = heart_alias_acquire(canon, IR0_O_WRONLY, &ops, &ctx, &dynamic);
    if (ret != 0)
        return ret;
    n = pseudo_fs_ops_write(ops, ctx, (const char *)buf, count);
    (void)pseudo_fs_release_ops(ops, ctx, dynamic);
    if (n < 0)
        return (int)n;
    *bytes_written = (size_t)n;
    return 0;
}

/*
 * The files are generated, with no stored size to cut: like sysfs, an
 * O_TRUNC open is accepted and changes nothing.
 */
static int heart_truncate(const char *path, size_t length)
{
    stat_t st;
    int ret;

    (void)length;
    ret = heart_stat(path, &st);
    if (ret != 0)
        return ret;
    return S_ISDIR(st.st_mode) ? -EISDIR : 0;
}

/* Linux lookup_open(): a directory without a create method is EACCES. */
static int heart_create(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return -EACCES;
}

static struct vfs_ops heartfs_ops = {
    .stat = heart_stat,
    .create = heart_create,
    .readdir = heart_readdir,
    .open = heart_open,
    .read = heart_read,
    .write = heart_write,
    .truncate = heart_truncate,
};

/*
 * Backend paths are absolute, so heartfs mounts only at /heart.
 * /heart/dennis stays on the filesystem that holds it (disk, with an
 * optional 9p share at /heart/dennis/src): bind it before /heart covers it.
 */
static int heartfs_mount(const char *dev, const char *dir)
{
    int ret;

    (void)dev;
    if (!dir || strcmp(dir, HEART_ROOT) != 0)
        return -EINVAL;
    ret = vfs_bind_mount(HEART_DENNIS, HEART_DENNIS);
    if (ret != 0 && ret != -EBUSY)
        return ret;
    return 0;
}

static int heartfs_umount(const char *dir)
{
    (void)dir;
    return -EBUSY;
}

static int heartfs_statfs(const char *path, struct ir0_statfs *buf)
{
    (void)path;
    buf->f_type = IR0_HEARTFS_MAGIC;
    buf->f_bsize = 4096;
    buf->f_frsize = 4096;
    buf->f_namelen = 255;
    return 0;
}

struct vfs_fstype heartfs_fs_type = {
    .name = "heartfs",
    .fs_flags = FS_NOSUID_DEFAULT,
    .ops = &heartfs_ops,
    .mount = heartfs_mount,
    .umount = heartfs_umount,
    .statfs = heartfs_statfs,
};
