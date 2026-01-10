/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - TMPFS (Temporary Filesystem)
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Simple in-memory temporary filesystem similar to Linux tmpfs
 * Perfect for /tmp or other temporary directories
 */

#include "vfs.h"
#include <ir0/memory/kmem.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ir0/stat.h>

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
} tmpfs_data_t;

static tmpfs_data_t *tmpfs_root = NULL;

static tmpfs_inode_t *tmpfs_alloc_inode(void)
{
    if (!tmpfs_root)
        return NULL;

    for (int i = 0; i < TMPFS_MAX_FILES; i++)
    {
        if (tmpfs_root->inodes[i].ino == 0)
        {
            tmpfs_inode_t *inode = &tmpfs_root->inodes[i];
            memset(inode, 0, sizeof(tmpfs_inode_t));
            inode->ino = ++tmpfs_root->next_ino;
            return inode;
        }
    }
    return NULL;
}

static int tmpfs_mount(const char *dev_name __attribute__((unused)),
                       const char *dir_name __attribute__((unused)))
{
    if (tmpfs_root)
        return 0;

    tmpfs_root = (tmpfs_data_t *)kmalloc(sizeof(tmpfs_data_t));
    if (!tmpfs_root)
        return -1;

    memset(tmpfs_root, 0, sizeof(tmpfs_data_t));

    /* Create root inode */
    tmpfs_inode_t *root_inode = tmpfs_alloc_inode();
    if (!root_inode)
    {
        kfree(tmpfs_root);
        tmpfs_root = NULL;
        return -1;
    }

    root_inode->mode = S_IFDIR | 0755;
    root_inode->is_dir = true;
    strcpy(root_inode->name, "/");
    root_inode->parent = NULL;
    tmpfs_root->root = root_inode;
    tmpfs_root->next_ino = 1; /* Root is inode 1 */

    extern void print(const char *str);
    print("TMPFS: Mounted successfully\n");

    return 0;
}

static struct filesystem_type tmpfs_fs_type = {
    .name = "tmpfs",
    .mount = tmpfs_mount,
    .next = NULL};

int tmpfs_register(void)
{
    extern int register_filesystem(struct filesystem_type * fs_type);
    return register_filesystem(&tmpfs_fs_type);
}

/* Helper function to find inode by path (simplified) */
static tmpfs_inode_t *tmpfs_lookup(const char *path)
{
    if (!tmpfs_root || !path || strcmp(path, "/") != 0)
        return NULL;

    return tmpfs_root->root;
}
