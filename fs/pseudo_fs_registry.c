/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pseudo_fs_registry.c
 * Description: Longest-prefix registry for /proc and /sys pseudo-fs endpoints.
 */

#include <ir0/pseudo_fs.h>
#include <ir0/errno.h>
#include <string.h>

static pseudo_fs_entry_t g_proc_entries[PSEUDO_FS_MAX_ENTRIES];
static pseudo_fs_entry_t g_sys_entries[PSEUDO_FS_MAX_ENTRIES];
static int g_proc_count;
static int g_sys_count;

static void pseudo_fs_normalize(const char *in, char *out, size_t out_sz)
{
    size_t len;

    if (!out || out_sz == 0)
        return;

    out[0] = '\0';
    if (!in || !in[0])
        return;

    if (in[0] != '/')
    {
        snprintf(out, out_sz, "/%s", in);
        return;
    }

    strncpy(out, in, out_sz - 1);
    out[out_sz - 1] = '\0';

    len = strlen(out);
    while (len > 1 && out[len - 1] == '/')
    {
        out[len - 1] = '\0';
        len--;
    }
}

static int pseudo_fs_build_full_path(const char *mount_prefix, const char *rel_path,
                                     char *full_path, size_t full_sz)
{
    char prefix[128];
    char rel[128];

    pseudo_fs_normalize(mount_prefix, prefix, sizeof(prefix));
    pseudo_fs_normalize(rel_path, rel, sizeof(rel));

    if (!prefix[0] || !rel[0])
        return -EINVAL;

    if (snprintf(full_path, full_sz, "%s%s", prefix, rel) >= (int)full_sz)
        return -ENAMETOOLONG;

    return 0;
}

static int pseudo_fs_register_table(pseudo_fs_entry_t *table, int *count, int fd_base,
                                    const char *mount_prefix, const char *rel_path,
                                    const pseudo_fs_ops_t *ops, void *ctx)
{
    pseudo_fs_entry_t *slot;
    char full_path[256];
    int rc;

    if (!table || !count || !mount_prefix || !rel_path || !ops)
        return -EINVAL;

    if (*count >= PSEUDO_FS_MAX_ENTRIES)
        return -ENOSPC;

    rc = pseudo_fs_build_full_path(mount_prefix, rel_path, full_path, sizeof(full_path));
    if (rc != 0)
        return rc;

    for (int i = 0; i < *count; i++)
    {
        if (table[i].in_use && strcmp(table[i].full_path, full_path) == 0)
            return -EEXIST;
    }

    slot = &table[*count];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->full_path, full_path, sizeof(slot->full_path) - 1);
    slot->full_path[sizeof(slot->full_path) - 1] = '\0';
    slot->ops = ops;
    slot->ctx = ctx;
    slot->fd = fd_base + *count;
    slot->in_use = 1;
    (*count)++;
    return 0;
}

static const pseudo_fs_entry_t *pseudo_fs_lookup_table(const pseudo_fs_entry_t *table,
                                                       int count, const char *full_path)
{
    char norm[256];
    size_t best_len = 0;
    const pseudo_fs_entry_t *best = NULL;

    if (!table || !full_path)
        return NULL;

    pseudo_fs_normalize(full_path, norm, sizeof(norm));

    for (int i = 0; i < count; i++)
    {
        size_t plen;

        if (!table[i].in_use)
            continue;

        plen = strlen(table[i].full_path);
        if (strncmp(table[i].full_path, norm, plen) != 0)
            continue;

        if (norm[plen] != '\0' && norm[plen] != '/')
            continue;

        if (plen >= best_len)
        {
            best_len = plen;
            best = &table[i];
        }
    }

    return best;
}

static const pseudo_fs_entry_t *pseudo_fs_find_by_fd_table(const pseudo_fs_entry_t *table,
                                                           int count, int fd)
{
    for (int i = 0; i < count; i++)
    {
        if (table[i].in_use && table[i].fd == fd)
            return &table[i];
    }

    return NULL;
}

int pseudo_fs_register(const char *mount_prefix, const char *rel_path,
                       const pseudo_fs_ops_t *ops, void *ctx)
{
    if (!mount_prefix || !rel_path)
        return -EINVAL;

    if (strncmp(mount_prefix, "/sys", 4) == 0)
    {
        return pseudo_fs_register_table(g_sys_entries, &g_sys_count, PSEUDO_FS_SYS_FD_BASE,
                                      mount_prefix, rel_path, ops, ctx);
    }

    return pseudo_fs_register_table(g_proc_entries, &g_proc_count, PSEUDO_FS_PROC_FD_BASE,
                                    mount_prefix, rel_path, ops, ctx);
}

const pseudo_fs_entry_t *pseudo_fs_lookup(const char *full_path)
{
    const pseudo_fs_entry_t *hit;

    if (!full_path)
        return NULL;

    hit = pseudo_fs_lookup_table(g_proc_entries, g_proc_count, full_path);
    if (hit)
        return hit;

    return pseudo_fs_lookup_table(g_sys_entries, g_sys_count, full_path);
}

const pseudo_fs_entry_t *pseudo_fs_find_by_fd(int fd)
{
    const pseudo_fs_entry_t *hit;

    hit = pseudo_fs_find_by_fd_table(g_proc_entries, g_proc_count, fd);
    if (hit)
        return hit;

    return pseudo_fs_find_by_fd_table(g_sys_entries, g_sys_count, fd);
}

void pseudo_fs_reset(void)
{
    memset(g_proc_entries, 0, sizeof(g_proc_entries));
    memset(g_sys_entries, 0, sizeof(g_sys_entries));
    g_proc_count = 0;
    g_sys_count = 0;
}

int pseudo_fs_proc_init(void)
{
    return 0;
}

int pseudo_fs_sys_init(void)
{
    return 0;
}

int64_t pseudo_fs_read_fd(int fd, char *buf, size_t count, off_t offset)
{
    static char full_buf[4096];
    const pseudo_fs_entry_t *entry;
    off_t inner = 0;
    int64_t full;
    size_t to_copy;

    if (!buf || count == 0)
        return 0;

    entry = pseudo_fs_find_by_fd(fd);
    if (!entry || !entry->ops || !entry->ops->read)
        return -EBADF;

    full = entry->ops->read(entry->ctx, full_buf, sizeof(full_buf), &inner);
    if (full < 0)
        return full;

    if (offset >= full)
        return 0;

    to_copy = (size_t)full - (size_t)offset;
    if (to_copy > count)
        to_copy = count;

    memcpy(buf, full_buf + (size_t)offset, to_copy);
    return (int64_t)to_copy;
}

int64_t pseudo_fs_write_fd(int fd, const char *buf, size_t count)
{
    const pseudo_fs_entry_t *entry;

    entry = pseudo_fs_find_by_fd(fd);
    if (!entry || !entry->ops || !entry->ops->write)
        return -EBADF;

    return entry->ops->write(entry->ctx, buf, count);
}

int64_t pseudo_fs_open_path(const char *full_path, int flags, int *out_fd)
{
    const pseudo_fs_entry_t *entry;
    int64_t rc;

    if (!full_path || !out_fd)
        return -EINVAL;

    entry = pseudo_fs_lookup(full_path);
    if (!entry)
        return -ENOENT;

    if (entry->ops && entry->ops->open)
    {
        rc = entry->ops->open(entry->ctx, flags);
        if (rc < 0)
            return rc;
    }

    *out_fd = entry->fd;
    return 0;
}

int64_t pseudo_fs_close_fd(int fd)
{
    const pseudo_fs_entry_t *entry;

    entry = pseudo_fs_find_by_fd(fd);
    if (!entry)
        return -EBADF;

    if (entry->ops && entry->ops->close)
        return entry->ops->close(entry->ctx);

    return 0;
}
