/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - TMPFS (Temporary Filesystem)
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Simple in-memory temporary filesystem similar to Linux tmpfs
 * Perfect for /tmp or other temporary directories
 * 
 * Features:
 * - Hierarchical directory structure
 * - Support for multiple mount points
 * - Maximum 128 files per instance
 * - Maximum 64KB per file
 * - Inode-based structure with parent-child relationships
 */

#include "vfs.h"
#include "tmpfs.h"
#include <ir0/kmem.h>
#include <ir0/vga.h>
#include <errno.h>
#include <string.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ir0/stat.h>
#include <ir0/errno.h>
#include <drivers/timer/clock_system.h>

/* Directory entry types (compatible with getdents) */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#define TMPFS_MAX_FILES 128
#define TMPFS_MAX_FILE_SIZE 65536 /* 64KB max per file */
#define TMPFS_MAX_NAME_LEN 255
#define TMPFS_MAX_DIRS 32

typedef struct tmpfs_inode
{
    uint32_t ino;
    uint16_t mode;
    uint32_t size;
    bool is_dir;
    char name[TMPFS_MAX_NAME_LEN + 1];
    uint8_t *data;
    struct tmpfs_inode *parent;
    struct tmpfs_inode *children;
    struct tmpfs_inode *sibling;
    time_t mtime;
} tmpfs_inode_t;

typedef struct tmpfs_data
{
    tmpfs_inode_t *root;
    tmpfs_inode_t inodes[TMPFS_MAX_FILES];
    uint32_t next_ino;
    size_t total_size;
    char mount_point[256];  /* Mount point path (e.g., "/tmp") */
} tmpfs_data_t;

/* Multiple TMPFS instances supported (one per mount point) */
static tmpfs_data_t *tmpfs_instances[TMPFS_MAX_DIRS] = {NULL};
static int num_tmpfs_instances = 0;

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
    if (!path)
        return NULL;
    
    /* Find the mount point that matches this path */
    for (int i = 0; i < num_tmpfs_instances; i++)
    {
        if (!tmpfs_instances[i])
            continue;
        
        size_t mp_len = strlen(tmpfs_instances[i]->mount_point);
        if (strncmp(path, tmpfs_instances[i]->mount_point, mp_len) == 0)
        {
            /* Check if path is exactly mount point or has trailing slash */
            if (path[mp_len] == '\0' || path[mp_len] == '/')
            {
                return tmpfs_instances[i];
            }
        }
    }
    
    return NULL;
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
    if (!tmpfs)
        return NULL;

    for (int i = 0; i < TMPFS_MAX_FILES; i++)
    {
        if (tmpfs->inodes[i].ino == 0)
        {
            tmpfs_inode_t *inode = &tmpfs->inodes[i];
            memset(inode, 0, sizeof(tmpfs_inode_t));
            inode->ino = ++tmpfs->next_ino;
            inode->mtime = (time_t)(clock_get_uptime_milliseconds() / 1000);
            return inode;
        }
    }
    return NULL;
}

static void tmpfs_free_inode(tmpfs_data_t *tmpfs, tmpfs_inode_t *inode)
{
    if (!tmpfs || !inode)
        return;
    
    /* Free data */
    if (inode->data)
    {
        kfree(inode->data);
        tmpfs->total_size -= inode->size;
    }
    
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
    
    /* Clear inode */
    memset(inode, 0, sizeof(tmpfs_inode_t));
}

static tmpfs_inode_t *tmpfs_lookup_inode(tmpfs_data_t *tmpfs, const char *rel_path)
{
    if (!tmpfs || !rel_path)
        return NULL;
    
    /* Root path */
    if (strcmp(rel_path, "/") == 0 || rel_path[0] == '\0')
        return tmpfs->root;
    
    /* Split path into components */
    char path_copy[TMPFS_MAX_NAME_LEN + 1];
    strncpy(path_copy, rel_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
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

static int tmpfs_mount(const char *dev_name __attribute__((unused)),
                       const char *dir_name)
{
    if (!dir_name)
        return -EINVAL;
    
    /* Check if already mounted */
    if (tmpfs_get_instance(dir_name))
        return 0;
    
    if (num_tmpfs_instances >= TMPFS_MAX_DIRS)
        return -ENOSPC;
    
    tmpfs_data_t *tmpfs = (tmpfs_data_t *)kmalloc(sizeof(tmpfs_data_t));
    if (!tmpfs)
        return -ENOMEM;
    
    memset(tmpfs, 0, sizeof(tmpfs_data_t));
    strncpy(tmpfs->mount_point, dir_name, sizeof(tmpfs->mount_point) - 1);
    tmpfs->mount_point[sizeof(tmpfs->mount_point) - 1] = '\0';
    
    /* Create root inode */
    tmpfs_inode_t *root_inode = tmpfs_alloc_inode(tmpfs);
    if (!root_inode)
    {
        kfree(tmpfs);
        return -ENOMEM;
    }
    
    root_inode->mode = S_IFDIR | 0755;
    root_inode->is_dir = true;
    strncpy(root_inode->name, "/", TMPFS_MAX_NAME_LEN);
    root_inode->name[TMPFS_MAX_NAME_LEN] = '\0';
    root_inode->parent = NULL;
    tmpfs->root = root_inode;
    tmpfs->next_ino = 1; /* Root is inode 1 */
    
    /* Add to instances */
    tmpfs_instances[num_tmpfs_instances++] = tmpfs;
    
    print("TMPFS: Mounted successfully at ");
    print(dir_name);
    print("\n");
    
    return 0;
}

/* TMPFS filesystem operations - exported for VFS */
struct filesystem_operations tmpfs_fs_ops = {
    .stat = tmpfs_stat,
    .mkdir = tmpfs_mkdir,
    .create_file = tmpfs_create_file,
    .unlink = tmpfs_unlink,
    .rmdir = tmpfs_rmdir,
    .readdir = tmpfs_readdir,
    .read_file = tmpfs_read_file,
    .write_file = tmpfs_write_file,
    .lookup = (struct vfs_inode *(*)(const char *))tmpfs_find_inode,
    .get_inode_number = tmpfs_get_inode_number,
    .ls = NULL,  /* TMPFS doesn't have ls, use readdir */
    .link = NULL,  /* TMPFS doesn't support links yet */
    .is_available = tmpfs_is_available,
    .is_working = tmpfs_is_available,
};

static struct filesystem_type tmpfs_fs_type = {
    .name = "tmpfs",
    .mount = tmpfs_mount,
    .ops = &tmpfs_fs_ops,
    .next = NULL
};

int tmpfs_register(void)
{
    return register_filesystem(&tmpfs_fs_type);
}

/* TMPFS API functions for VFS integration */

bool tmpfs_is_available(void)
{
    return num_tmpfs_instances > 0;
}

tmpfs_inode_t *tmpfs_find_inode(const char *path)
{
    tmpfs_data_t *tmpfs = tmpfs_get_instance_for_path(path);
    if (!tmpfs)
        return NULL;
    
    const char *rel_path = tmpfs_get_relative_path(path, tmpfs->mount_point);
    return tmpfs_lookup_inode(tmpfs, rel_path);
}

uint32_t tmpfs_get_inode_number(const char *path)
{
    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    return inode ? inode->ino : 0;
}

int tmpfs_stat(const char *path, stat_t *buf)
{
    if (!path || !buf)
        return -EINVAL;
    
    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;
    
    memset(buf, 0, sizeof(stat_t));
    buf->st_ino = inode->ino;
    buf->st_mode = inode->mode;
    buf->st_size = inode->size;
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_mtime = inode->mtime;
    
    return 0;
}

int tmpfs_mkdir(const char *path, mode_t mode)
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
    char path_copy[256];
    strncpy(path_copy, rel_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash)
        return -EINVAL;
    
    *last_slash = '\0';
    const char *dirname = last_slash + 1;
    
    if (strlen(dirname) == 0 || strlen(dirname) > TMPFS_MAX_NAME_LEN)
        return -EINVAL;
    
    /* Find parent */
    const char *parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    tmpfs_inode_t *parent = tmpfs_lookup_inode(tmpfs, parent_path);
    if (!parent || !parent->is_dir)
        return -ENOENT;
    
    /* Allocate new inode */
    tmpfs_inode_t *new_inode = tmpfs_alloc_inode(tmpfs);
    if (!new_inode)
        return -ENOSPC;
    
    new_inode->mode = S_IFDIR | (mode & 0777);
    new_inode->is_dir = true;
    strncpy(new_inode->name, dirname, TMPFS_MAX_NAME_LEN);
    new_inode->name[TMPFS_MAX_NAME_LEN] = '\0';
    new_inode->parent = parent;
    new_inode->sibling = parent->children;
    parent->children = new_inode;
    
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
    char path_copy[256];
    strncpy(path_copy, rel_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash)
        return -EINVAL;
    
    *last_slash = '\0';
    const char *filename = last_slash + 1;
    
    if (strlen(filename) == 0 || strlen(filename) > TMPFS_MAX_NAME_LEN)
        return -EINVAL;
    
    /* Find parent */
    const char *parent_path = (path_copy[0] == '\0') ? "/" : path_copy;
    tmpfs_inode_t *parent = tmpfs_lookup_inode(tmpfs, parent_path);
    if (!parent || !parent->is_dir)
        return -ENOENT;
    
    /* Allocate new inode */
    tmpfs_inode_t *new_inode = tmpfs_alloc_inode(tmpfs);
    if (!new_inode)
        return -ENOSPC;
    
    new_inode->mode = S_IFREG | (mode & 0777);
    new_inode->is_dir = false;
    new_inode->size = 0;
    new_inode->data = NULL;
    strncpy(new_inode->name, filename, TMPFS_MAX_NAME_LEN);
    new_inode->name[TMPFS_MAX_NAME_LEN] = '\0';
    new_inode->parent = parent;
    new_inode->sibling = parent->children;
    parent->children = new_inode;
    
    return 0;
}

int tmpfs_read_file(const char *path, void *buf, size_t count, size_t *read_count, off_t offset)
{
    if (!path || !buf)
        return -EINVAL;
    
    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;
    
    if (inode->is_dir)
        return -EISDIR;
    
    if (offset < 0 || (size_t)offset >= inode->size)
        return 0;  /* EOF */
    
    size_t available = inode->size - (size_t)offset;
    size_t to_read = (count < available) ? count : available;
    
    if (to_read > 0 && inode->data)
    {
        memcpy(buf, inode->data + offset, to_read);
    }
    
    if (read_count)
        *read_count = to_read;
    
    return 0;
}

int tmpfs_write_file(const char *path, const void *buf, size_t count, size_t *written_count, off_t offset)
{
    if (!path || !buf)
        return -EINVAL;
    
    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;
    
    if (inode->is_dir)
        return -EISDIR;
    
    /* Calculate new size */
    size_t new_size = (size_t)offset + count;
    if (new_size > TMPFS_MAX_FILE_SIZE)
        return -EFBIG;
    
    /* Reallocate if needed */
    if (new_size > inode->size)
    {
        uint8_t *new_data = (uint8_t *)kmalloc(new_size);
        if (!new_data)
            return -ENOMEM;
        
        /* Copy existing data */
        if (inode->data)
        {
            memcpy(new_data, inode->data, inode->size);
            kfree(inode->data);
        }
        
        /* Zero out new space */
        memset(new_data + inode->size, 0, new_size - inode->size);
        
        inode->data = new_data;
        inode->size = new_size;
    }
    
    /* Write data */
    if (count > 0)
    {
        memcpy(inode->data + offset, buf, count);
        inode->mtime = (time_t)(clock_get_uptime_milliseconds() / 1000);
    }
    
    if (written_count)
        *written_count = count;
    
    return 0;
}

int tmpfs_unlink(const char *path)
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

int tmpfs_rmdir(const char *path)
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

int tmpfs_readdir(const char *path, struct vfs_dirent_readdir *entries, int max_entries)
{
    if (!path || !entries || max_entries <= 0)
        return -EINVAL;
    
    tmpfs_inode_t *inode = tmpfs_find_inode(path);
    if (!inode)
        return -ENOENT;
    
    if (!inode->is_dir)
        return -ENOTDIR;
    
    int count = 0;
    tmpfs_inode_t *child = inode->children;
    
    while (child && count < max_entries)
    {
        strncpy(entries[count].name, child->name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].inode = child->ino;
        entries[count].type = child->is_dir ? DT_DIR : DT_REG;
        
        count++;
        child = child->sibling;
    }
    
    return count;
}
