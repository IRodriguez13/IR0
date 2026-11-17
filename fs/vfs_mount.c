/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vfs_mount.c
 * Description: VFS mount table — filesystem registry, mount,
 *              remount, umount, pivot_root, root mount and statfs.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "vfs.h"
#include <ir0/kmem.h>
#include <ir0/cred.h>
#include <ir0/blockdev.h>
#include <ir0/partition.h>
#include <ir0/path_routed.h>
#include <ir0/klog.h>
#include <ir0/statfs.h>
#include <ir0/process.h>
#include <string.h>
#include "vfs_internal.h"

/* ------------------------------------------------------------------ */
/*  Global state                                                       */
/* ------------------------------------------------------------------ */
static struct vfs_fstype *fs_types  = NULL;
static struct vfs_mount  *mounts   = NULL;

/**
 * Find the mount whose path is the longest prefix of @path.
 */
struct vfs_mount *vfs_find_mount(const char *path)
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

int vfs_mount_is_readonly(const char *path)
{
    struct vfs_mount *m = vfs_find_mount(path);

    return m && (m->flags & IR0_MS_RDONLY);
}

/*
 * Set-user-ID / set-group-ID on exec is allowed only on mounts that are not
 * MS_NOSUID and expose a real on-disk backend (vfs_ops). Pseudo / nodev
 * filesystems default to nosuid at mount time — not by comparing fs name.
 */
int vfs_path_allows_setid(const char *path)
{
    struct vfs_mount *m;

    if (!path)
        return 0;

    m = vfs_find_mount(path);
    if (!m || !m->fs || !m->fs->ops)
        return 0;

    if (m->flags & IR0_MS_NOSUID)
        return 0;

    return 1;
}

static unsigned long vfs_default_mount_flags(const struct vfs_fstype *ft)
{
    return (ft->fs_flags & FS_NOSUID_DEFAULT) ? IR0_MS_NOSUID : 0;
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
/*  proc — Linux nodev mount succeeds; paths stay on pseudo /proc     */
/* ------------------------------------------------------------------ */

static int proc_fs_mount(const char *dev, const char *dir)
{
    (void)dev;

    if (!dir || (strcmp(dir, "/proc") != 0 && strcmp(dir, "/proc/") != 0))
        return -EINVAL;
    return 0;
}

static int proc_fs_umount(const char *dir)
{
    if (!dir || (strcmp(dir, "/proc") != 0 && strcmp(dir, "/proc/") != 0))
        return -EINVAL;
    return -EBUSY;
}

static struct vfs_fstype proc_fs_type = {
    .name   = "proc",
    .fs_flags = FS_NOSUID_DEFAULT,
    .ops    = NULL,
    .mount  = proc_fs_mount,
    .umount = proc_fs_umount,
    .next   = NULL,
};

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/* Linux register_filesystem(): append @fs, keeping registration order. */
static int vfs_register_fs(struct vfs_fstype *fs)
{
    struct vfs_fstype **link = &fs_types;

    if (!fs || !fs->name || strlen(fs->name) == 0)
        return -EINVAL;

    for (; *link; link = &(*link)->next)
        if (strcmp((*link)->name, fs->name) == 0)
            return -EEXIST;

    fs->next = NULL;
    *link = fs;
    return 0;
}

static int vfs_init(void)
{
    fs_types = NULL;
    mounts   = NULL;
    (void)vfs_register_fs(&proc_fs_type);
    for (size_t i = 0; vfs_builtin_filesystems[i]; i++)
        (void)vfs_register_fs(vfs_builtin_filesystems[i]);
    if (DEBUG_VFS)
    {
        klog_info("VFS", "CLASSIFY VFS_FS_CONTRACT_ACTIVE");
        klog_info("VFS", "CLASSIFY VFS_FS_CONTRACT_DOCUMENTED");
        klog_info("VFS", "CLASSIFY SYSCALLS_USE_VFS_ONLY");
        klog_info("VFS", "CLASSIFY FUTURE_FS_READY");
    }
    return 0;
}

struct vfs_mount *vfs_get_mounts(void)
{
    return mounts;
}

int vfs_sync(void)
{
    /*
     * Best-effort: flush every registered block device. Per-FS sync hooks
     * can be added to vfs_ops later; MINIX already writes through on mutate.
     */
    ir0_block_flush_all();
    return 0;
}

/* Partition @partno (1-based) of the registered disk @disk. */
static int lookup_partition(const char *disk, unsigned long partno,
                            uint64_t *start)
{
    partition_info_t pi;

    /* The partition table is indexed by the legacy disk slot (Q12). */
    for (uint8_t d = 0; d < MAX_DISKS; d++) {
        const char *name = ir0_block_legacy_name(d);

        if (!name || strcmp(name, disk) != 0)
            continue;
        if (partno == 0 || partno > MAX_PARTITIONS_PER_DISK ||
            get_partition_info(d, (uint8_t)(partno - 1), &pi) != 0)
            return -ENODEV;
        *start = pi.start_lba;
        return 0;
    }
    return -ENODEV;
}

int lookup_bdev(const char *source, struct vfs_bdev *bdev)
{
    const char *name;
    size_t len;
    size_t base;
    stat_t st;
    int ret;

    if (!source || !source[0] || !bdev)
        return -EINVAL;

    memset(bdev, 0, sizeof(*bdev));
    name = strncmp(source, "/dev/", 5) == 0 ? source + 5 : source;
    len = strlen(name);

    if (len < sizeof(bdev->disk) && ir0_block_name_is_present(name)) {
        memcpy(bdev->disk, name, len + 1);
        return 0;
    }

    /* "<disk><n>": partition n of a registered disk. */
    base = len;
    while (base > 0 && name[base - 1] >= '0' && name[base - 1] <= '9')
        base--;
    if (base > 0 && base < len && base < sizeof(bdev->disk)) {
        unsigned long partno = 0;

        memcpy(bdev->disk, name, base);
        bdev->disk[base] = '\0';
        if (ir0_block_name_is_present(bdev->disk)) {
            for (size_t i = base; i < len && partno <= MAX_PARTITIONS_PER_DISK; i++)
                partno = partno * 10 + (unsigned long)(name[i] - '0');
            return lookup_partition(bdev->disk, partno, &bdev->start);
        }
        bdev->disk[0] = '\0';
    }

    /* Not a block device: tell a missing path from a file of another type. */
    ret = ir0_stat_path_routed_follow(source, &st);
    if (ret != 0)
        return ret;
    return S_ISBLK(st.st_mode) ? -ENXIO : -ENOTBLK;
}

static struct vfs_fstype *vfs_find_fstype(const char *name)
{
    for (struct vfs_fstype *t = fs_types; t; t = t->next) {
        if (strcmp(t->name, name) == 0 ||
            (t->alias && strcmp(t->alias, name) == 0))
            return t;
    }
    return NULL;
}

int vfs_mount(const char *dev, const char *path, const char *fstype)
{
    char mount_path[MAX_PATH];

    if (!fstype || !path)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;

    int ret = vfs_validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));

    /* Linux do_new_mount(): an unknown type is -ENODEV before anything else. */
    struct vfs_fstype *ft = vfs_find_fstype(fstype);
    if (!ft)
        return -ENODEV;

    for (struct vfs_mount *existing = mounts; existing; existing = existing->next)
    {
        if (strcmp(existing->path, mount_path) == 0)
            return -EBUSY;
    }

    struct vfs_mount *m = kmalloc(sizeof(*m));
    if (!m)
        return -ENOMEM;

    strncpy(m->path, mount_path, sizeof(m->path) - 1);
    m->path[sizeof(m->path) - 1] = '\0';
    strncpy(m->backend_path, mount_path, sizeof(m->backend_path) - 1);
    m->backend_path[sizeof(m->backend_path) - 1] = '\0';
    if (dev) {
        strncpy(m->dev, dev, sizeof(m->dev) - 1);
        m->dev[sizeof(m->dev) - 1] = '\0';
    } else {
        m->dev[0] = '\0';
    }
    m->fs = ft;
    m->flags = vfs_default_mount_flags(ft);
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

int vfs_remount(const char *path, unsigned long flags)
{
    char mount_path[MAX_PATH];
    struct vfs_mount *m;
    int ret;

    if (!path)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;

    ret = vfs_validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));
    m = vfs_find_mount(mount_path);
    if (!m)
        return -EINVAL;

    /* Only RO bit is toggled today; other MS_* bits are ignored honestly. */
    if (flags & IR0_MS_RDONLY)
        m->flags |= IR0_MS_RDONLY;
    else
        m->flags &= ~IR0_MS_RDONLY;
    return 0;
}

int vfs_umount(const char *path)
{
    char mount_path[MAX_PATH];
    struct vfs_mount *prev = NULL;
    struct vfs_mount *victim = NULL;
    int ret;

    if (!path)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;

    ret = vfs_validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));
    if (strcmp(mount_path, "/") == 0)
        return -EBUSY;

    for (struct vfs_mount *m = mounts; m; m = m->next)
    {
        if (strcmp(m->path, mount_path) == 0)
        {
            victim = m;
            break;
        }
        prev = m;
    }

    if (!victim)
        return -ENOENT;

    for (struct vfs_mount *m = mounts; m; m = m->next)
    {
        size_t vlen = strlen(victim->path);
        if (m == victim)
            continue;
        if (strncmp(m->path, victim->path, vlen) == 0 &&
            m->path[vlen] == '/')
            return -EBUSY;
    }

    if (victim->fs && victim->fs->umount)
    {
        int ret = victim->fs->umount(victim->path);
        if (ret != 0)
            return ret;
    }

    if (prev)
        prev->next = victim->next;
    else
        mounts = victim->next;

    kfree(victim);
    return 0;
}

static struct vfs_mount *vfs_mount_exact(const char *path)
{
    struct vfs_mount *m;

    for (m = mounts; m; m = m->next)
    {
        if (strcmp(m->path, path) == 0)
            return m;
    }
    return NULL;
}

static int vfs_path_below(const char *path, const char *parent)
{
    size_t parent_len;

    if (!path || !parent)
        return 0;
    parent_len = strlen(parent);
    return strncmp(path, parent, parent_len) == 0 &&
           path[parent_len] == '/';
}

static int vfs_pivot_mount_path(const struct vfs_mount *m,
                                const struct vfs_mount *new_mount,
                                const char *new_root, const char *old_root,
                                char *out, size_t out_size)
{
    const char *suffix;
    size_t old_len;
    size_t suffix_len;

    if (m == new_mount)
    {
        if (out_size < 2)
            return -ENAMETOOLONG;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }
    if (vfs_path_below(m->path, new_root))
    {
        suffix = m->path + strlen(new_root);
        if (strlen(suffix) + 1 > out_size)
            return -ENAMETOOLONG;
        memcpy(out, suffix, strlen(suffix) + 1);
        return 0;
    }

    suffix = strcmp(m->path, "/") == 0 ? "" : m->path;
    old_len = strlen(old_root);
    suffix_len = strlen(suffix);
    if (old_len + suffix_len + 1 > out_size)
        return -ENAMETOOLONG;
    memcpy(out, old_root, old_len);
    memcpy(out + old_len, suffix, suffix_len + 1);
    return 0;
}

int vfs_pivot_root(const char *new_root, const char *put_old)
{
    char new_path[VFS_PATH_MAX];
    char old_path[VFS_PATH_MAX];
    char candidate[VFS_PATH_MAX];
    struct vfs_mount *new_mount;
    struct vfs_mount *m;
    struct vfs_mount *other;
    stat_t st;
    size_t new_len;

    if (!new_root || !put_old)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;
    if (vfs_validate_path(new_root) != 0 || vfs_validate_path(put_old) != 0)
        return -EINVAL;
    normalize_mount_path(new_root, new_path, sizeof(new_path));
    normalize_mount_path(put_old, old_path, sizeof(old_path));
    if (strcmp(new_path, "/") == 0)
        return -EBUSY;
    new_len = strlen(new_path);
    if (strncmp(old_path, new_path, new_len) != 0 ||
        old_path[new_len] != '/')
        return -EINVAL;
    if (vfs_stat(new_path, &st) != 0 || !S_ISDIR(st.st_mode) ||
        vfs_stat(old_path, &st) != 0 || !S_ISDIR(st.st_mode))
        return -ENOTDIR;
    if (!vfs_mount_exact("/"))
        return -EINVAL;
    new_mount = vfs_mount_exact(new_path);
    if (!new_mount)
        return -EINVAL;

    for (m = mounts; m; m = m->next)
    {
        int ret = vfs_pivot_mount_path(m, new_mount, new_path,
                                       old_path + new_len,
                                       candidate, sizeof(candidate));
        if (ret != 0)
            return ret;
        for (other = mounts; other != m; other = other->next)
        {
            char other_path[VFS_PATH_MAX];

            ret = vfs_pivot_mount_path(other, new_mount, new_path,
                                       old_path + new_len,
                                       other_path, sizeof(other_path));
            if (ret != 0)
                return ret;
            if (strcmp(candidate, other_path) == 0)
                return -EBUSY;
        }
    }

    for (m = mounts; m; m = m->next)
    {
        if (vfs_pivot_mount_path(m, new_mount, new_path,
                                 old_path + new_len,
                                 candidate, sizeof(candidate)) != 0)
            return -EIO;
        strncpy(m->path, candidate, sizeof(m->path) - 1);
        m->path[sizeof(m->path) - 1] = '\0';
    }
    return 0;
}

/*
 * Mount the configured root; when that is impossible, fall back to a tmpfs
 * root (an error if tmpfs is not built in either).
 */
static int vfs_mount_fallback_root(const char *why)
{
    int ret;

    klog_info("VFS", why);
    ret = vfs_mount("none", "/", "tmpfs");
    if (ret != 0) {
        klog_error("VFS", "tmpfs fallback root failed");
        return ret;
    }
    klog_info("VFS", "tmpfs root mounted (fallback)");
    return 0;
}

int vfs_init_root(void)
{
    const char *root_blk = CONFIG_ROOT_BLOCK_DEVICE;
    const char *root_fs = CONFIG_ROOT_FILESYSTEM;
    char root_dev_path[64];
    int ret;

    klog_info("VFS", "Initializing VFS...");
    ret = vfs_init();
    if (ret != 0) {
        klog_error("VFS", "vfs_init failed");
        return ret;
    }
    klog_info("VFS", "vfs_init OK (builtin filesystems registered)");

    snprintf(root_dev_path, sizeof(root_dev_path), "/dev/%s", root_blk);

    /* A missing disk only matters when the root type is built in. */
    if (vfs_find_fstype(root_fs) && !ir0_block_name_is_present(root_blk)) {
        klog_error("VFS", "No block device configured for root available");
        return vfs_mount_fallback_root("falling back to tmpfs root (block device missing)");
    }

    klog_info("VFS", "Mounting root filesystem...");
    ret = vfs_mount(root_dev_path, "/", root_fs);
    if (ret != 0)
        return vfs_mount_fallback_root("configured root mount failed, falling back to tmpfs root");
    klog_info("VFS", "vfs_mount OK (configured root)");
    return 0;
}

int vfs_statfs(const char *path, struct ir0_statfs *buf)
{
	struct vfs_mount *m;
	int ret;

	if (!path || !buf)
		return -EINVAL;
	ret = vfs_validate_path(path);
	if (ret != 0)
		return ret;

	/*
	 * Pseudo-filesystems are not VFS mounts, so vfs_find_mount() below would
	 * attribute them to the root mount and make df report /proc with the
	 * size of the disk. They hold no blocks; report the Linux magic and
	 * zeros, which is also what df needs to skip them without -a.
	 */
	{
		static const struct
		{
			const char *prefix;
			uint32_t magic;
		} pseudo[] = {
			{ "/proc", IR0_PROC_SUPER_MAGIC },
			{ "/sys", IR0_SYSFS_MAGIC },
			{ "/dev", IR0_TMPFS_MAGIC },
			{ "/heart", IR0_HEARTFS_MAGIC },
		};
		size_t i;

		for (i = 0; i < sizeof(pseudo) / sizeof(pseudo[0]); i++)
		{
			size_t plen = strlen(pseudo[i].prefix);

			if (strncmp(path, pseudo[i].prefix, plen) != 0)
				continue;
			if (path[plen] != '\0' && path[plen] != '/')
				continue;

			memset(buf, 0, sizeof(*buf));
			buf->f_type = pseudo[i].magic;
			buf->f_bsize = 4096;
			buf->f_frsize = 4096;
			buf->f_namelen = 255;
			return 0;
		}
	}

	m = vfs_find_mount(path);
	if (!m || !m->fs || !m->fs->name)
		return -ENODEV;

	memset(buf, 0, sizeof(*buf));
	if (m->fs->statfs)
	{
		vfs_fs_op_acquire();
		ret = m->fs->statfs(path, buf);
		vfs_fs_op_release();
		return ret;
	}

	/* No statfs: honest empty stats (df skips f_blocks==0 unless -a). */
	buf->f_bsize = 1024;
	buf->f_frsize = 1024;
	buf->f_namelen = 255;
	return 0;
}
