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
#if CONFIG_ENABLE_FS_MINIX
#include "minix_fs.h"
#endif
#if CONFIG_ENABLE_FS_TMPFS
#include "tmpfs.h"
#include "hostshare_9p.h"
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
#include "simplefs.h"
#endif
#if CONFIG_ENABLE_FS_FAT16
#include "fat16_fs.h"
#endif
#if CONFIG_ENABLE_FS_EXT2
#include "ext2_disk.h"
#endif
#include <ir0/kmem.h>
#include <ir0/credentials.h>
#include <ir0/blockdev.h>
#include <ir0/klog.h>
#include <ir0/statfs.h>
#include <ir0/virtio_9p.h>
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

int vfs_mount_is_nosuid(const char *path)
{
    struct vfs_mount *m = vfs_find_mount(path);

    if (!m)
        return 1;
    return (m->flags & IR0_MS_NOSUID) != 0;
}

static unsigned long vfs_default_mount_flags(const char *mount_path,
                                             const char *fstype)
{
    (void)mount_path;

    if (!fstype)
        return IR0_MS_NOSUID;

    if (strcmp(fstype, "proc") == 0 || strcmp(fstype, "sysfs") == 0 ||
        strcmp(fstype, "tmpfs") == 0 || strcmp(fstype, "ramfs") == 0)
        return IR0_MS_NOSUID;

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
    .ops    = NULL,
    .mount  = proc_fs_mount,
    .umount = proc_fs_umount,
    .next   = NULL,
};

static void proc_fs_register(void)
{
    (void)vfs_register_fs(&proc_fs_type);
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
    hostshare_9p_register();
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
    simplefs_register();
#endif
#if CONFIG_ENABLE_FS_FAT16
    fat16_fs_register();
#endif
#if CONFIG_ENABLE_FS_EXT2
    ext2_fs_register();
#endif
    proc_fs_register();
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

int vfs_mount(const char *dev, const char *path, const char *fstype)
{
    char mount_path[MAX_PATH];
    const char *resolved_fstype = fstype;

    if (!fstype || !path)
        return -EINVAL;
    if (ir0_current_cred() && !ir0_cred_is_root())
        return -EPERM;

    int ret = vfs_validate_path(path);
    if (ret != 0)
        return ret;

    normalize_mount_path(path, mount_path, sizeof(mount_path));

    /* ramfs is intentionally an alias of tmpfs in IR0 runtime mounts. */
    if (strcmp(resolved_fstype, "ramfs") == 0)
        resolved_fstype = "tmpfs";

    /* Linux do_new_mount(): an unknown type is -ENODEV before anything else. */
    struct vfs_fstype *ft = NULL;
    for (struct vfs_fstype *t = fs_types; t; t = t->next) {
        if (strcmp(t->name, resolved_fstype) == 0) {
            ft = t;
            break;
        }
    }
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
    m->flags = vfs_default_mount_flags(mount_path, resolved_fstype);
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

int vfs_init_root(void)
{
    const char *root_blk = CONFIG_ROOT_BLOCK_DEVICE;
    const char *root_fs = CONFIG_ROOT_FILESYSTEM;
    char root_dev_path[64];
    int can_use_block_dev = 1;

#define VFS_MSG(s) klog_info("VFS", (s))
#define VFS_ERR(s) klog_error("VFS", (s))

    VFS_MSG("Initializing VFS...");
    int ret = vfs_init();
    if (ret != 0) {
        VFS_ERR("vfs_init failed");
        return ret;
    }
    VFS_MSG("vfs_init OK (builtin filesystems registered)");

    snprintf(root_dev_path, sizeof(root_dev_path), "/dev/%s", root_blk);

#if !CONFIG_ENABLE_FS_MINIX
    if (strcmp(root_fs, "minix") == 0)
        can_use_block_dev = 0;
#endif
#if !CONFIG_ENABLE_FS_EXT2
    if (strcmp(root_fs, "ext2") == 0)
        can_use_block_dev = 0;
#endif

    if (can_use_block_dev && !ir0_block_name_is_present(root_blk)) {
        VFS_ERR("No block device configured for root available");
#if CONFIG_ENABLE_FS_TMPFS
        VFS_MSG("falling back to tmpfs root (block device missing)");
        ret = vfs_mount("none", "/", "tmpfs");
        if (ret != 0) {
            VFS_ERR("tmpfs fallback also failed");
            return ret;
        }
        VFS_MSG("tmpfs root mounted (fallback, no block device)");
        return 0;
#else
        return -ENODEV;
#endif
    }

    VFS_MSG("Mounting root filesystem...");
    ret = vfs_mount(root_dev_path, "/", root_fs);
    if (ret != 0) {
        VFS_MSG("configured root mount failed, falling back to tmpfs root");
#if CONFIG_ENABLE_FS_TMPFS
        ret = vfs_mount("none", "/", "tmpfs");
        if (ret != 0) {
            VFS_ERR("tmpfs fallback also failed");
            return ret;
        }
        VFS_MSG("tmpfs root mounted (fallback)");
#else
        VFS_ERR("tmpfs fallback disabled by configuration");
        return ret;
#endif
    } else {
        VFS_MSG("vfs_mount OK (configured root)");
    }
#undef VFS_MSG
#undef VFS_ERR
    return 0;
}

int vfs_init_with_minix(void)
{
    return vfs_init_root();
}

int vfs_statfs(const char *path, struct ir0_statfs *buf)
{
	struct vfs_mount *m;
	const char *fst;
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
	fst = m->fs->name;

#if CONFIG_ENABLE_FS_MINIX
	if (strcmp(fst, "minix") == 0)
	{
		int rc;

		vfs_fs_op_acquire();
		rc = minix_fs_statfs(buf);
		vfs_fs_op_release();
		return rc;
	}
#endif
#if CONFIG_ENABLE_FS_TMPFS
	if (strcmp(fst, "tmpfs") == 0)
	{
		int rc;

		vfs_fs_op_acquire();
		rc = tmpfs_statfs(path, buf);
		vfs_fs_op_release();
		return rc;
	}
	if (strcmp(fst, "9p") == 0)
	{
		struct virtio_9p_statfs h;

		buf->f_type = IR0_9P_MAGIC;
		buf->f_bsize = 4096;
		buf->f_frsize = 4096;
		buf->f_blocks = 0;
		buf->f_bfree = 0;
		buf->f_bavail = 0;
		buf->f_namelen = 255;

		/*
		 * Ask the host. Falling back to the zeros above keeps df
		 * working against a server without Tstatfs instead of failing
		 * the whole call.
		 */
		if (virtio_9p_statfs(&h) == 0 && h.bsize != 0)
		{
			buf->f_bsize = h.bsize;
			buf->f_frsize = h.bsize;
			buf->f_blocks = h.blocks;
			buf->f_bfree = h.bfree;
			buf->f_bavail = h.bavail;
			buf->f_files = h.files;
			buf->f_ffree = h.ffree;
			if (h.namelen)
				buf->f_namelen = h.namelen;
		}
		return 0;
	}
#endif
	/* Unknown FS: honest empty stats (df skips f_blocks==0 unless -a). */
	buf->f_bsize = 1024;
	buf->f_frsize = 1024;
	buf->f_namelen = 255;
	return 0;
}
