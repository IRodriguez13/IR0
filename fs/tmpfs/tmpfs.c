/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: tmpfs.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fs/vfs.h>
#include "tmpfs.h"
#include <ir0/kmem.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <ir0/errno.h>
#include <ir0/inode.h>
#include <ir0/clock.h>
#include <ir0/cred.h>
#include <ir0/mm_alloc.h>
#include <ir0/mm_port.h>
#include <ir0/page.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>

static int tmpfs_stat(const char *path, stat_t *buf);
static int tmpfs_mkdir(const char *path, mode_t mode);
static int tmpfs_link(const char *oldpath, const char *newpath);
static int tmpfs_unlink(const char *path);
static int tmpfs_rmdir(const char *path);
static int tmpfs_chown(const char *path, uid_t owner, gid_t group);
static int tmpfs_truncate(const char *path, size_t length);
static int tmpfs_readdir(const char *path, struct vfs_dirent *entries, int max_entries);

#define TMPFS_MAX_NAME_LEN 255
#define TMPFS_MAX_DIRS 32
#define TMPFS_PAGE ((size_t)IR0_MM_PAGE_SIZE)
/* Largest file size a write may reach (the page table is one array). */
#define TMPFS_MAX_WRITE_PAGES ((size_t)1 << 20)

/*
 * Regular-file contents live in whole pages (Linux shmem): pages[i] is the
 * physical frame holding bytes [i * TMPFS_PAGE, (i + 1) * TMPFS_PAGE), or 0
 * for a hole that reads as zeros. Only written pages are allocated.
 */
typedef struct tmpfs_file
{
    uint32_t ino;
    uint16_t mode;
    uint32_t uid;
    uint32_t gid;
    size_t size;
    size_t npages;     /* slots in pages[] */
    uintptr_t *pages;
    size_t nr_frames;  /* allocated frames, for st_blocks */
    time_t mtime;
    uint32_t nlink;
    struct tmpfs_data *sb; /* charged mount; NULL once it is unmounted */
    struct tmpfs_file *hnext;
} tmpfs_file_t;

typedef struct tmpfs_inode
{
    uint32_t ino;
    uint16_t mode;
    uint32_t uid;
    uint32_t gid;
    bool is_dir;
    char name[TMPFS_MAX_NAME_LEN + 1];
    tmpfs_file_t *file; /* shared regular-file inode; NULL for directories */
    struct tmpfs_inode *parent;
    struct tmpfs_inode *children;
    struct tmpfs_inode *sibling;
    time_t mtime;
} tmpfs_inode_t;

/*
 * One mount. Linux shmem limits: size and nr_inodes both default to half
 * of RAM (in pages); every name counts as an inode, as in shmem_link().
 */
typedef struct tmpfs_data
{
    tmpfs_inode_t *root;
    size_t nr_inodes;
    size_t max_inodes;
    size_t nr_pages;
    size_t max_pages;
    char mount_point[256];  /* Mount point path (e.g., "/tmp") */
} tmpfs_data_t;

/* Multiple TMPFS instances supported (one per mount point) */
static tmpfs_data_t *tmpfs_instances[TMPFS_MAX_DIRS] = {NULL};
static int num_tmpfs_instances = 0;

/*
 * Inode numbers are unique across mounts, so a regular file is found by
 * number alone (vfs_inode_ops) until mounts carry a super_block. Every
 * regular file, named or orphaned, is in tmpfs_files. The fs-op lock
 * guards both, like every other tmpfs structure.
 */
#define TMPFS_FILE_BUCKETS 64
static uint32_t tmpfs_last_ino = 1; /* 1 is every mount's root */
static tmpfs_file_t *tmpfs_files[TMPFS_FILE_BUCKETS];

static const struct vfs_inode_ops tmpfs_inode_ops;

static int tmpfs_copy_path(char *dest, size_t dest_size, const char *src)
{
    size_t len;

    if (!dest || dest_size == 0 || !src)
        return -EINVAL;
    len = strlen(src);
    if (len >= dest_size)
        return -ENAMETOOLONG;
    memcpy(dest, src, len + 1);
    return 0;
}

/* Get TMPFS instance for a given mount point */
static tmpfs_data_t *tmpfs_get_instance(const char *mount_point)
{
    for (int i = 0; i < num_tmpfs_instances; i++)
    {
        if (tmpfs_instances[i] && strcmp(tmpfs_instances[i]->mount_point, mount_point) == 0)
        {
            return tmpfs_instances[i];
        }
    }
    return NULL;
}

/* Get TMPFS instance for a given path */
static tmpfs_data_t *tmpfs_get_instance_for_path(const char *path)
{
    tmpfs_data_t *best = NULL;
    size_t best_len = 0;

    if (!path)
        return NULL;
    
    /* Find the mount point that matches this path */
    for (int i = 0; i < num_tmpfs_instances; i++)
    {
        if (!tmpfs_instances[i])
            continue;

        size_t mp_len = strlen(tmpfs_instances[i]->mount_point);
        if (mp_len <= best_len)
            continue;
        if (strncmp(path, tmpfs_instances[i]->mount_point, mp_len) != 0)
            continue;
        if (!(mp_len == 1 && tmpfs_instances[i]->mount_point[0] == '/') &&
            path[mp_len] != '\0' && path[mp_len] != '/')
            continue;
        best = tmpfs_instances[i];
        best_len = mp_len;
    }

    return best;
}

/* Extract relative path from full path */
static const char *tmpfs_get_relative_path(const char *full_path, const char *mount_point)
{
    if (!full_path || !mount_point)
        return NULL;
    
    size_t mp_len = strlen(mount_point);
    if (strncmp(full_path, mount_point, mp_len) != 0)
        return NULL;
    
    const char *rel_path = full_path + mp_len;
    
    /* Skip leading slashes */
    while (*rel_path == '/')
        rel_path++;
    
    /* If empty, return "/" for root */
    if (*rel_path == '\0')
        return "/";
    
    return rel_path;
}

static tmpfs_inode_t *tmpfs_alloc_inode(tmpfs_data_t *tmpfs)
{
    tmpfs_inode_t *inode;

    if (!tmpfs || tmpfs->nr_inodes >= tmpfs->max_inodes)
        return NULL;
    inode = (tmpfs_inode_t *)kmalloc(sizeof(*inode));
    if (!inode)
        return NULL;
    memset(inode, 0, sizeof(*inode));
    inode->ino = ++tmpfs_last_ino;
    inode->mtime = clock_get_current_time();
    tmpfs->nr_inodes++;
    return inode;
}

/* Give back an inode that was never linked into a directory. */
static void tmpfs_drop_inode(tmpfs_data_t *tmpfs, tmpfs_inode_t *inode)
{
    kfree(inode);
    if (tmpfs->nr_inodes > 0)
        tmpfs->nr_inodes--;
}

/* Free the frames of pages [first, npages) and uncharge them. */
static void tmpfs_free_pages_from(tmpfs_file_t *file, size_t first)
{
    tmpfs_data_t *tmpfs = file->sb;

    for (size_t i = first; i < file->npages; i++)
    {
        if (!file->pages[i])
            continue;
        pmm_free_frame(file->pages[i]);
        file->pages[i] = 0;
        file->nr_frames--;
        if (tmpfs && tmpfs->nr_pages > 0)
            tmpfs->nr_pages--;
    }
}

static tmpfs_file_t **tmpfs_file_slot(uint32_t ino)
{
    return &tmpfs_files[ino % TMPFS_FILE_BUCKETS];
}

static tmpfs_file_t *tmpfs_file_by_ino(ino_t ino)
{
    tmpfs_file_t *file;

    for (file = *tmpfs_file_slot((uint32_t)ino); file; file = file->hnext)
    {
        if (file->ino == ino)
            return file;
    }
    return NULL;
}

static void tmpfs_file_free(tmpfs_file_t *file)
{
    tmpfs_file_t **link;

    for (link = tmpfs_file_slot(file->ino); *link; link = &(*link)->hnext)
    {
        if (*link == file)
        {
            *link = file->hnext;
            break;
        }
    }
    tmpfs_free_pages_from(file, 0);
    kfree(file->pages);
    kfree(file);
}

/*
 * A name of @file went. The last one frees it, unless the VFS still holds
 * it: then tmpfs_ievict() does at the last close (Linux evict()).
 */
static void tmpfs_file_unlink(tmpfs_file_t *file)
{
    if (file->nlink > 0)
        file->nlink--;
    if (file->nlink == 0 && !inode_orphan(&tmpfs_inode_ops, file->ino))
        tmpfs_file_free(file);
}

static void tmpfs_free_inode(tmpfs_data_t *tmpfs, tmpfs_inode_t *inode)
{
    if (!tmpfs || !inode)
        return;
    
    /* Regular-file contents and metadata are shared by hardlinks. */
    if (inode->file)
        tmpfs_file_unlink(inode->file);
    
    /* Remove from parent's children list */
    if (inode->parent)
    {
        tmpfs_inode_t *prev = NULL;
        tmpfs_inode_t *child = inode->parent->children;
        
        while (child && child != inode)
        {
            prev = child;
            child = child->sibling;
        }
        
        if (child == inode)
        {
            if (prev)
                prev->sibling = inode->sibling;
            else
                inode->parent->children = inode->sibling;
        }
    }
    
    /* Free children recursively */
    tmpfs_inode_t *child = inode->children;
    while (child)
    {
        tmpfs_inode_t *next = child->sibling;
        tmpfs_free_inode(tmpfs, child);
        child = next;
    }
    
    tmpfs_drop_inode(tmpfs, inode);
}

/*
 * Append @inode to @parent's list. Inode numbers only grow, so the list
 * stays in inode order and readdir can hand out the inode number as a
 * stable position cookie (vfs_dirent.off).
 */
static void tmpfs_add_child(tmpfs_inode_t *parent, tmpfs_inode_t *inode)
{
    tmpfs_inode_t **link = &parent->children;

    while (*link)
        link = &(*link)->sibling;
    inode->parent = parent;
    inode->sibling = NULL;
    *link = inode;
}

static tmpfs_inode_t *tmpfs_lookup_inode(tmpfs_data_t *tmpfs, const char *rel_path)
{
    if (!tmpfs || !rel_path)
        return NULL;
    
    /* Root path */
    if (strcmp(rel_path, "/") == 0 || rel_path[0] == '\0')
        return tmpfs->root;
    
    /* Split path into components */
    char path_copy[VFS_PATH_MAX];
    if (tmpfs_copy_path(path_copy, sizeof(path_copy), rel_path) != 0)
        return NULL;
    
    tmpfs_inode_t *current = tmpfs->root;
    char *token = path_copy;
    char *next_token;
    
    while (*token == '/')
        token++;
    
    while (token && *token != '\0')
    {
        /* Extract next component */
        next_token = strchr(token, '/');
        if (next_token)
        {
            *next_token = '\0';
            next_token++;
        }
        
        /* Find inode in current directory */
        if (!current->is_dir)
            return NULL;
        
        tmpfs_inode_t *found = NULL;
        tmpfs_inode_t *child = current->children;
        while (child)
        {
            if (strcmp(child->name, token) == 0)
            {
                found = child;
                break;
            }
            child = child->sibling;
        }
        
        if (!found)
            return NULL;
        
        current = found;
        
        /* Skip leading slashes in next token */
        token = next_token;
        while (token && *token == '/')
            token++;
    }
    
    return current;
}

/* Linux shmem_default_max_blocks/_inodes: half of RAM, in pages. */
static void tmpfs_default_limits(tmpfs_data_t *tmpfs)
{
    size_t total = 0;
    size_t used = 0;
    size_t free_frames = 0;

    ir0_mm_pmm_stats(&total, &used, &free_frames);
    tmpfs->max_pages = total / 2;
    tmpfs->max_inodes = total / 2;
    if (tmpfs->max_inodes < 1)
        tmpfs->max_inodes = 1;
}

static int tmpfs_mount(const char *dev_name __attribute__((unused)),
                       const char *dir_name)
{
    if (!dir_name)
        return -EINVAL;

    if (strlen(dir_name) >= sizeof(((tmpfs_data_t *)0)->mount_point))
        return -ENAMETOOLONG;

    if (tmpfs_get_instance(dir_name))
        return 0;

    if (num_tmpfs_instances >= TMPFS_MAX_DIRS)
        return -ENOSPC;

    tmpfs_data_t *tmpfs = (tmpfs_data_t *)kmalloc(sizeof(tmpfs_data_t));
    if (!tmpfs)
        return -ENOMEM;

    memset(tmpfs, 0, sizeof(tmpfs_data_t));
    tmpfs_default_limits(tmpfs);
    strncpy(tmpfs->mount_point, dir_name, sizeof(tmpfs->mount_point) - 1);
    tmpfs->mount_point[sizeof(tmpfs->mount_point) - 1] = '\0';

    tmpfs_inode_t *root_inode = tmpfs_alloc_inode(tmpfs);
    if (!root_inode) {
        kfree(tmpfs);
        return -ENOMEM;
    }

    root_inode->mode = S_IFDIR | 0755;
    root_inode->uid = ROOT_UID;
    root_inode->gid = ROOT_GID;
    root_inode->is_dir = true;
    strncpy(root_inode->name, "/", TMPFS_MAX_NAME_LEN);
    root_inode->name[TMPFS_MAX_NAME_LEN] = '\0';
    root_inode->parent = NULL;
    root_inode->ino = 1;
    tmpfs->root = root_inode;

    tmpfs_instances[num_tmpfs_instances++] = tmpfs;
    return 0;
}

static int tmpfs_umount(const char *dir_name)
{
    tmpfs_data_t *tmpfs = NULL;
    int idx = -1;

    if (!dir_name)
        return -EINVAL;

    for (int i = 0; i < num_tmpfs_instances; i++)
    {
        if (tmpfs_instances[i] &&
            strcmp(tmpfs_instances[i]->mount_point, dir_name) == 0)
        {
            idx = i;
            tmpfs = tmpfs_instances[i];
            break;
        }
    }

    if (!tmpfs)
        return -ENOENT;

    if (tmpfs->root)
        tmpfs_free_inode(tmpfs, tmpfs->root);
    /* Files still open outlive the mount; nothing charges them now. */
    for (int b = 0; b < TMPFS_FILE_BUCKETS; b++)
    {
        for (tmpfs_file_t *f = tmpfs_files[b]; f; f = f->hnext)
        {
            if (f->sb == tmpfs)
                f->sb = NULL;
        }
    }

    kfree(tmpfs);

    for (int i = idx; i < num_tmpfs_instances - 1; i++)
        tmpfs_instances[i] = tmpfs_instances[i + 1];

    tmpfs_instances[num_tmpfs_instances - 1] = NULL;
    num_tmpfs_instances--;
    return 0;
}

static struct vfs_ops tmpfs_fs_ops = {
    .stat    = tmpfs_stat,
    .mkdir   = tmpfs_mkdir,
    .create  = tmpfs_create_file,
    .unlink  = tmpfs_unlink,
    .link    = tmpfs_link,
    .rmdir   = tmpfs_rmdir,
    .readdir = tmpfs_readdir,
    .read    = tmpfs_read_file,
    .write   = tmpfs_write_file,
    .chown   = tmpfs_chown,
    .chmod   = tmpfs_chmod,
    .truncate = tmpfs_truncate,
    .iops    = &tmpfs_inode_ops,
};

/* ramfs is an alias of tmpfs: the same in-memory driver under both names. */
struct vfs_fstype tmpfs_fs_type = {
    .name  = "tmpfs",
    .alias = "ramfs",
    .fs_flags = FS_NOSUID_DEFAULT,
    .ops   = &tmpfs_fs_ops,
    .mount = tmpfs_mount,
    .umount = tmpfs_umount,
    .statfs = tmpfs_statfs,
    .next  = NULL,
};

/* TMPFS API functions for VFS integration */

tmpfs_inode_t *tmpfs_find_inode(const char *path)
{
    tmpfs_data_t *tmpfs = tmpfs_get_instance_for_path(path);
    if (!tmpfs)
        return NULL;
    
    const char *rel_path = tmpfs_get_relative_path(path, tmpfs->mount_point);
    return tmpfs_lookup_inode(tmpfs, rel_path);
}
static void tmpfs_file_stat(const tmpfs_file_t *file, stat_t *buf)
{
    memset(buf, 0, sizeof(stat_t));
    buf->st_ino = file->ino;
    buf->st_mode = file->mode;
    buf->st_size = file->size;
    buf->st_nlink = file->nlink;
    buf->st_uid = file->uid;
    buf->st_gid = file->gid;
    buf->st_mtime = file->mtime;
    buf->st_blocks = file->nr_frames * (TMPFS_PAGE / 512);
}

static int tmpfs_stat(const char *path, stat_t *buf)
{
    if (!path || !buf)
        return -EINVAL;
    
    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;
    
    memset(buf, 0, sizeof(stat_t));
    if (inode->file)
    {
        tmpfs_file_stat(inode->file, buf);
    }
    else
    {
        buf->st_ino = inode->ino;
        buf->st_mode = inode->mode;
        buf->st_size = 0;
        buf->st_nlink = 1;
        buf->st_uid = inode->uid;
        buf->st_gid = inode->gid;
        buf->st_mtime = inode->mtime;
    }
    
    return 0;
}

static int tmpfs_mkdir(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;
    
    tmpfs_data_t *tmpfs = tmpfs_get_instance_for_path(path);
    if (!tmpfs)
        return -ENODEV;
    
    const char *rel_path = tmpfs_get_relative_path(path, tmpfs->mount_point);
    if (!rel_path)
        return -EINVAL;
    
    /* Check if already exists */
    if (tmpfs_lookup_inode(tmpfs, rel_path))
        return -EEXIST;
    
    /* Split into parent and name */
    char path_copy[VFS_PATH_MAX];
    if (tmpfs_copy_path(path_copy, sizeof(path_copy), rel_path) != 0)
        return -ENAMETOOLONG;
    
    char *last_slash = strrchr(path_copy, '/');
    const char *dirname = NULL;
    const char *parent_path = NULL;
    if (!last_slash)
    {
        dirname = path_copy;
        parent_path = "/";
    }
    else
    {
        *last_slash = '\0';
        dirname = last_slash + 1;
        parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    }
    
    if (strlen(dirname) == 0)
        return -EINVAL;
    if (strlen(dirname) > TMPFS_MAX_NAME_LEN)
        return -ENAMETOOLONG;
    
    /* Find parent */
    tmpfs_inode_t *parent = tmpfs_lookup_inode(tmpfs, parent_path);
    if (!parent || !parent->is_dir)
        return -ENOENT;
    
    /* Allocate new inode */
    tmpfs_inode_t *new_inode = tmpfs_alloc_inode(tmpfs);
    if (!new_inode)
        return -ENOSPC;

    {
        const struct ir0_task_cred *cr = ir0_current_cred();
        mode_t umask_value = (mode_t)(cr ? cr->umask : DEFAULT_UMASK);
        mode_t effective_mode = mode & ~umask_value;

        new_inode->mode = S_IFDIR | (effective_mode & 0777);
        new_inode->uid = cr ? cr->euid : ROOT_UID;
        new_inode->gid = cr ? cr->egid : ROOT_GID;
    }
    new_inode->is_dir = true;
    strncpy(new_inode->name, dirname, TMPFS_MAX_NAME_LEN);
    new_inode->name[TMPFS_MAX_NAME_LEN] = '\0';
    tmpfs_add_child(parent, new_inode);
    
    return 0;
}

int tmpfs_create_file(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;
    
    tmpfs_data_t *tmpfs = tmpfs_get_instance_for_path(path);
    if (!tmpfs)
        return -ENODEV;
    
    const char *rel_path = tmpfs_get_relative_path(path, tmpfs->mount_point);
    if (!rel_path)
        return -EINVAL;
    
    /* Check if already exists */
    if (tmpfs_lookup_inode(tmpfs, rel_path))
        return -EEXIST;
    
    /* Split into parent and name */
    char path_copy[VFS_PATH_MAX];
    if (tmpfs_copy_path(path_copy, sizeof(path_copy), rel_path) != 0)
        return -ENAMETOOLONG;
    
    char *last_slash = strrchr(path_copy, '/');
    const char *filename = NULL;
    const char *parent_path = NULL;
    if (!last_slash)
    {
        filename = path_copy;
        parent_path = "/";
    }
    else
    {
        *last_slash = '\0';
        filename = last_slash + 1;
        parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    }
    
    if (strlen(filename) == 0)
        return -EINVAL;
    if (strlen(filename) > TMPFS_MAX_NAME_LEN)
        return -ENAMETOOLONG;
    
    /* Find parent */
    tmpfs_inode_t *parent = tmpfs_lookup_inode(tmpfs, parent_path);
    if (!parent || !parent->is_dir)
        return -ENOENT;
    
    /* Allocate new inode */
    tmpfs_inode_t *new_inode = tmpfs_alloc_inode(tmpfs);
    if (!new_inode)
        return -ENOSPC;

    {
        const struct ir0_task_cred *cr = ir0_current_cred();
        mode_t umask_value = (mode_t)(cr ? cr->umask : DEFAULT_UMASK);
        mode_t effective_mode = mode & ~umask_value;
        tmpfs_file_t *file = (tmpfs_file_t *)kmalloc(sizeof(*file));

        if (!file)
        {
            tmpfs_drop_inode(tmpfs, new_inode);
            return -ENOMEM;
        }
        memset(file, 0, sizeof(*file));
        file->ino = new_inode->ino;
        file->mode = S_IFREG | (effective_mode & 0777);
        file->uid = cr ? cr->euid : ROOT_UID;
        file->gid = cr ? cr->egid : ROOT_GID;
        file->mtime = clock_get_current_time();
        file->nlink = 1;
        file->sb = tmpfs;
        file->hnext = *tmpfs_file_slot(file->ino);
        *tmpfs_file_slot(file->ino) = file;
        new_inode->file = file;
    }
    new_inode->is_dir = false;
    strncpy(new_inode->name, filename, TMPFS_MAX_NAME_LEN);
    new_inode->name[TMPFS_MAX_NAME_LEN] = '\0';
    tmpfs_add_child(parent, new_inode);
    
    return 0;
}

/* The regular file @path names, or NULL with *err set. */
static tmpfs_file_t *tmpfs_file_at(const char *path, int *err)
{
    tmpfs_inode_t *inode = tmpfs_find_inode(path);

    *err = -ENOENT;
    if (!inode)
        return NULL;
    *err = inode->is_dir ? -EISDIR : -EIO;
    return inode->file;
}

static int tmpfs_file_read(tmpfs_file_t *file, void *buf, size_t count,
                           size_t *read_count, off_t offset)
{
    size_t pos;
    size_t to_read;
    size_t done = 0;

    if (read_count)
        *read_count = 0;
    if (!buf || offset < 0)
        return -EINVAL;

    pos = (size_t)offset;
    if (pos >= file->size)
        return 0;  /* EOF */
    to_read = file->size - pos;
    if (count < to_read)
        to_read = count;

    while (done < to_read)
    {
        size_t idx = (pos + done) / TMPFS_PAGE;
        size_t in_page = (pos + done) % TMPFS_PAGE;
        size_t chunk = TMPFS_PAGE - in_page;

        if (chunk > to_read - done)
            chunk = to_read - done;
        if (idx < file->npages && file->pages[idx])
            memcpy((uint8_t *)buf + done,
                   (const uint8_t *)phys_to_virt(file->pages[idx]) + in_page,
                   chunk);
        else
            memset((uint8_t *)buf + done, 0, chunk);
        done += chunk;
    }
    
    if (read_count)
        *read_count = done;
    
    return 0;
}

int tmpfs_read_file(const char *path, void *buf, size_t count, size_t *read_count, off_t offset)
{
    tmpfs_file_t *file;
    int err;

    if (read_count)
        *read_count = 0;
    if (!path)
        return -EINVAL;
    file = tmpfs_file_at(path, &err);
    if (!file)
        return err;
    return tmpfs_file_read(file, buf, count, read_count, offset);
}

/* Make pages[] hold at least @need slots (geometric growth). */
static int tmpfs_reserve_slots(tmpfs_file_t *file, size_t need)
{
    size_t cap;
    uintptr_t *slots;

    if (need <= file->npages)
        return 0;
    if (need > TMPFS_MAX_WRITE_PAGES)
        return -EFBIG;
    cap = file->npages ? file->npages : 4;
    while (cap < need)
        cap *= 2;
    if (cap > TMPFS_MAX_WRITE_PAGES)
        cap = TMPFS_MAX_WRITE_PAGES;
    slots = (uintptr_t *)kcalloc(cap, sizeof(*slots));
    if (!slots)
        return -ENOMEM;
    if (file->pages)
    {
        memcpy(slots, file->pages, file->npages * sizeof(*slots));
        kfree(file->pages);
    }
    file->pages = slots;
    file->npages = cap;
    return 0;
}

/* The frame for page @idx, allocated (zeroed and charged) on first use. */
static uint8_t *tmpfs_page_for_write(tmpfs_file_t *file, size_t idx, int *err)
{
    tmpfs_data_t *tmpfs = file->sb;
    uintptr_t phys;

    if (file->pages[idx])
        return (uint8_t *)phys_to_virt(file->pages[idx]);
    if (tmpfs && tmpfs->nr_pages >= tmpfs->max_pages)
    {
        *err = -ENOSPC;
        return NULL;
    }
    phys = mm_alloc_frame(MM_ALLOC_DIRECT_RECLAIM);
    if (!phys)
    {
        *err = -ENOSPC;
        return NULL;
    }
    paging_zero_phys_page(phys);
    file->pages[idx] = phys;
    file->nr_frames++;
    if (tmpfs)
        tmpfs->nr_pages++;
    return (uint8_t *)phys_to_virt(phys);
}

static int tmpfs_file_write(tmpfs_file_t *file, const void *buf, size_t count,
                            size_t *written_count, off_t offset)
{
    size_t pos;
    size_t done = 0;
    int ret;

    if (written_count)
        *written_count = 0;
    if (!buf || offset < 0)
        return -EINVAL;

    if (count == 0)
        return 0;

    pos = (size_t)offset;
    if (pos > TMPFS_MAX_WRITE_PAGES * TMPFS_PAGE ||
        count > TMPFS_MAX_WRITE_PAGES * TMPFS_PAGE - pos)
        return -EFBIG;
    ret = tmpfs_reserve_slots(file, (pos + count + TMPFS_PAGE - 1) / TMPFS_PAGE);
    if (ret != 0)
        return ret;

    /* Linux generic_perform_write: a short write stops at the first error. */
    while (done < count)
    {
        size_t idx = (pos + done) / TMPFS_PAGE;
        size_t in_page = (pos + done) % TMPFS_PAGE;
        size_t chunk = TMPFS_PAGE - in_page;
        uint8_t *page;
        int err = 0;

        if (chunk > count - done)
            chunk = count - done;
        page = tmpfs_page_for_write(file, idx, &err);
        if (!page)
        {
            if (done == 0)
                return err;
            break;
        }
        memcpy(page + in_page, (const uint8_t *)buf + done, chunk);
        done += chunk;
    }

    if (pos + done > file->size)
        file->size = pos + done;
    file->mtime = clock_get_current_time();
    if (written_count)
        *written_count = done;
    
    return 0;
}

int tmpfs_write_file(const char *path, const void *buf, size_t count, size_t *written_count, off_t offset)
{
    tmpfs_file_t *file;
    int err;

    if (written_count)
        *written_count = 0;
    if (!path)
        return -EINVAL;
    file = tmpfs_file_at(path, &err);
    if (!file)
        return err;
    return tmpfs_file_write(file, buf, count, written_count, offset);
}

static int tmpfs_link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath)
        return -EINVAL;

    tmpfs_data_t *tmpfs = tmpfs_get_instance_for_path(oldpath);
    if (!tmpfs || tmpfs_get_instance_for_path(newpath) != tmpfs)
        return -EXDEV;

    const char *old_rel = tmpfs_get_relative_path(oldpath, tmpfs->mount_point);
    const char *new_rel = tmpfs_get_relative_path(newpath, tmpfs->mount_point);
    if (!old_rel || !new_rel)
        return -EINVAL;

    tmpfs_inode_t *src = tmpfs_lookup_inode(tmpfs, old_rel);
    if (!src)
        return -ENOENT;
    if (src->is_dir)
        return -EPERM;
    if (!src->file)
        return -EIO;
    if (tmpfs_lookup_inode(tmpfs, new_rel))
        return -EEXIST;

    char path_copy[VFS_PATH_MAX];
    if (tmpfs_copy_path(path_copy, sizeof(path_copy), new_rel) != 0)
        return -ENAMETOOLONG;

    char *last_slash = strrchr(path_copy, '/');
    const char *linkname = NULL;
    const char *parent_path = NULL;
    if (!last_slash)
    {
        linkname = path_copy;
        parent_path = "/";
    }
    else
    {
        *last_slash = '\0';
        linkname = last_slash + 1;
        parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    }

    if (strlen(linkname) == 0)
        return -EINVAL;
    if (strlen(linkname) > TMPFS_MAX_NAME_LEN)
        return -ENAMETOOLONG;

    tmpfs_inode_t *parent = tmpfs_lookup_inode(tmpfs, parent_path);
    if (!parent || !parent->is_dir)
        return -ENOENT;

    tmpfs_inode_t *dst = tmpfs_alloc_inode(tmpfs);
    if (!dst)
        return -ENOSPC;

    dst->is_dir = false;
    dst->file = src->file;
    dst->file->nlink++;
    strncpy(dst->name, linkname, TMPFS_MAX_NAME_LEN);
    dst->name[TMPFS_MAX_NAME_LEN] = '\0';
    tmpfs_add_child(parent, dst);

    return 0;
}

static int tmpfs_unlink(const char *path)
{
    if (!path)
        return -EINVAL;
    
    tmpfs_data_t *tmpfs = tmpfs_get_instance_for_path(path);
    if (!tmpfs)
        return -ENODEV;
    
    const char *rel_path = tmpfs_get_relative_path(path, tmpfs->mount_point);
    if (!rel_path)
        return -EINVAL;
    
    tmpfs_inode_t *inode = tmpfs_lookup_inode(tmpfs, rel_path);
    if (!inode)
        return -ENOENT;
    
    if (inode->is_dir)
        return -EISDIR;
    
    /* Cannot unlink root */
    if (inode == tmpfs->root)
        return -EINVAL;
    
    tmpfs_free_inode(tmpfs, inode);
    return 0;
}

static int tmpfs_rmdir(const char *path)
{
    if (!path)
        return -EINVAL;
    
    tmpfs_data_t *tmpfs = tmpfs_get_instance_for_path(path);
    if (!tmpfs)
        return -ENODEV;
    
    const char *rel_path = tmpfs_get_relative_path(path, tmpfs->mount_point);
    if (!rel_path)
        return -EINVAL;
    
    tmpfs_inode_t *inode = tmpfs_lookup_inode(tmpfs, rel_path);
    if (!inode)
        return -ENOENT;
    
    if (!inode->is_dir)
        return -ENOTDIR;
    
    /* Cannot remove root */
    if (inode == tmpfs->root)
        return -EINVAL;
    
    /* Directory must be empty */
    if (inode->children)
        return -ENOTEMPTY;
    
    tmpfs_free_inode(tmpfs, inode);
    return 0;
}

/*
 * tmpfs_chmod - Replace permission bits; keep file type bits in st_mode.
 */
int tmpfs_chmod(const char *path, mode_t mode)
{
    if (!path)
        return -EINVAL;

    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;

    if (inode->file)
        inode->file->mode = (uint16_t)(((uint32_t)inode->file->mode & ~07777U) |
                           ((uint32_t)mode & 07777U));
    else
        inode->mode = (uint16_t)(((uint32_t)inode->mode & ~07777U) |
                     ((uint32_t)mode & 07777U));
    return 0;
}

static int tmpfs_chown(const char *path, uid_t owner, gid_t group)
{
    if (!path)
        return -EINVAL;

    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;

    if (inode->file)
    {
        if (owner != (uid_t)-1)
            inode->file->uid = (uint32_t)owner;
        if (group != (gid_t)-1)
            inode->file->gid = (uint32_t)group;
        inode->file->mtime = clock_get_current_time();
    }
    else
    {
        if (owner != (uid_t)-1)
            inode->uid = (uint32_t)owner;
        if (group != (gid_t)-1)
            inode->gid = (uint32_t)group;
        inode->mtime = clock_get_current_time();
    }
    return 0;
}

/*
 * tmpfs_truncate - Set a regular file's length (Linux shmem_setattr). A
 * longer file reads zeros past the old end without allocating; a shorter
 * one frees whole pages past the new end and zeroes the rest of the last
 * page, so growing it again shows zeros.
 */
static void tmpfs_file_truncate(tmpfs_file_t *file, size_t length)
{
    if (length < file->size)
    {
        size_t keep = (length + TMPFS_PAGE - 1) / TMPFS_PAGE;
        size_t tail = length % TMPFS_PAGE;

        tmpfs_free_pages_from(file, keep);
        if (tail != 0 && keep - 1 < file->npages && file->pages[keep - 1])
            memset((uint8_t *)phys_to_virt(file->pages[keep - 1]) + tail, 0,
                   TMPFS_PAGE - tail);
    }
    file->size = length;
    file->mtime = clock_get_current_time();
}

static int tmpfs_truncate(const char *path, size_t length)
{
    tmpfs_file_t *file;
    int err;

    if (!path)
        return -EINVAL;
    file = tmpfs_file_at(path, &err);
    if (!file)
        return err;
    tmpfs_file_truncate(file, length);
    return 0;
}

/* vfs_inode_ops: a regular file by number, named or orphaned. */
static int tmpfs_istat(ino_t ino, stat_t *buf)
{
    tmpfs_file_t *file = tmpfs_file_by_ino(ino);

    if (!file || !buf)
        return -ENOENT;
    tmpfs_file_stat(file, buf);
    return 0;
}

static int tmpfs_iread(ino_t ino, void *buf, size_t count, size_t *done,
                       off_t offset)
{
    tmpfs_file_t *file = tmpfs_file_by_ino(ino);

    if (!file)
        return -ENOENT;
    return tmpfs_file_read(file, buf, count, done, offset);
}

static int tmpfs_iwrite(ino_t ino, const void *buf, size_t count,
                        size_t *done, off_t offset)
{
    tmpfs_file_t *file = tmpfs_file_by_ino(ino);

    if (!file)
        return -ENOENT;
    return tmpfs_file_write(file, buf, count, done, offset);
}

static int tmpfs_itruncate(ino_t ino, size_t length)
{
    tmpfs_file_t *file = tmpfs_file_by_ino(ino);

    if (!file)
        return -ENOENT;
    tmpfs_file_truncate(file, length);
    return 0;
}

static void tmpfs_ievict(ino_t ino)
{
    tmpfs_file_t *file = tmpfs_file_by_ino(ino);

    if (file && file->nlink == 0)
        tmpfs_file_free(file);
}

static const struct vfs_inode_ops tmpfs_inode_ops = {
    .stat     = tmpfs_istat,
    .read     = tmpfs_iread,
    .write    = tmpfs_iwrite,
    .truncate = tmpfs_itruncate,
    .evict    = tmpfs_ievict,
};

int tmpfs_statfs(const char *path, struct ir0_statfs *buf)
{
    tmpfs_data_t *tmpfs;

    if (!path || !buf)
        return -EINVAL;

    tmpfs = tmpfs_get_instance_for_path(path);
    if (!tmpfs)
        return -ENODEV;

    memset(buf, 0, sizeof(*buf));
    buf->f_type = IR0_TMPFS_MAGIC;
    buf->f_bsize = TMPFS_PAGE;
    buf->f_frsize = TMPFS_PAGE;
    buf->f_blocks = tmpfs->max_pages;
    buf->f_bfree = tmpfs->max_pages - tmpfs->nr_pages;
    buf->f_bavail = buf->f_bfree;
    buf->f_files = tmpfs->max_inodes;
    buf->f_ffree = tmpfs->max_inodes - tmpfs->nr_inodes;
    buf->f_namelen = TMPFS_MAX_NAME_LEN;
    return 0;
}

static int tmpfs_readdir(const char *path, struct vfs_dirent *entries, int max_entries)
{
    if (!path || !entries || max_entries <= 0)
        return -EINVAL;

    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;

    if (!inode->is_dir)
        return -ENOTDIR;

    int count = 0;

    /* Standard "." and ".." first (DT_DIR), then children */
    if (count < max_entries) {
        strncpy(entries[count].name, ".", sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].type = DT_DIR;
        count++;
    }
    if (count < max_entries) {
        strncpy(entries[count].name, "..", sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].type = DT_DIR;
        count++;
    }

    tmpfs_inode_t *child = inode->children;

    while (child && count < max_entries) {
        strncpy(entries[count].name, child->name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].type = child->is_dir ? DT_DIR : DT_REG;
        entries[count].off = child->ino;
        count++;
        child = child->sibling;
    }

    return count;
}
