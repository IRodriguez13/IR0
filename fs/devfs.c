/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: devfs.c
 * Description: /dev core — registry, init, path lookup and node lifecycle.
 */

#include "devfs_internal.h"

#include <ir0/console.h>
#if CONFIG_ENABLE_SOUND
#include <ir0/audio_backend.h>
#endif
#include <ir0/input_backend.h>
#if CONFIG_ENABLE_NETWORKING
#include <ir0/net.h>
#endif
#include <ir0/blockdev.h>
#include <ir0/partition.h>
#include <string.h>
#include <ir0/clock.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/arch_cpu.h>
#include <ir0/klog.h>
#include <ir0/serial_io.h>
#include <ir0/ipc.h>
#include <ir0/arch_cpu.h>
#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif
#include <ir0/video_backend.h>
#include <ir0/copy_user.h>
#include <ir0/input.h>
#include <ir0/credentials.h>
#include <ir0/fb.h>
#include <ir0/ktm/userdev.h>
#include <ir0/io_async.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <linux/vt.h>
#include "vfs.h"
#include <ir0/devfs.h>

pid_t devfs_current_pid(void)
{
    return ir0_current_pid();
}

/* Device registry — room for builtins + dense disk/part topology */
#define MAX_DEV_NODES 224
static devfs_node_t *dev_nodes[MAX_DEV_NODES];
static int num_dev_nodes = 0;

#if CONFIG_ENABLE_NETWORKING
static int devfs_net_can_read(devfs_entry_t *entry, pid_t pid)
{
    (void)entry;
    return dev_net_pid_has_ready_ping(pid);
}
#endif

static int devfs_poll_default(int (*hook)(devfs_entry_t *, pid_t),
                              devfs_node_t *node, pid_t pid)
{
    if (node && node->ops && hook)
        return hook(&node->entry, pid);
    return 1;
}

int devfs_fd_can_read(uint32_t device_id, pid_t pid)
{
    devfs_node_t *node = devfs_find_node_by_id(device_id);

    return devfs_poll_default(
        node && node->ops ? node->ops->can_read : NULL, node, pid);
}

static const devfs_ops_t net_ops = {
    .read = dev_net_read,
    .write = dev_net_write,
    .ioctl = dev_net_ioctl,
#if CONFIG_ENABLE_NETWORKING
    .can_read = devfs_net_can_read,
#endif
};

static const devfs_ops_t disk_ops = {
    .read = dev_disk_read,
    .write = dev_disk_write,
    .ioctl = dev_disk_ioctl,
};

devfs_node_t dev_net = {
    .entry = { .name = "net", .mode = 0660, .device_id = 8 },
    .ops = &net_ops,
    .ref_count = 0
};

devfs_node_t dev_disk = {
    .entry = { .name = "disk", .mode = 0660, .device_id = 9 },
    .ops = &disk_ops,
    .ref_count = 0
};

/**
 * devfs_register_node - Registra un nodo pre-asignado en devfs
 * @node: Nodo con entry y ops ya configurados
 *
 * Para dispositivos con struct estática. Para dispositivos dinámicos usar
 * devfs_register_device.
 */
int devfs_register_node(devfs_node_t *node)
{
    if (!node || num_dev_nodes >= MAX_DEV_NODES)
        return -1;
    if (devfs_find_node_by_id(node->entry.device_id))
    {
        klog_print("DEVFS_DUP_DEVICE_ID\n");
        return -EEXIST;
    }
    dev_nodes[num_dev_nodes++] = node;
    return 0;
}

static int devfs_register_node_checked(devfs_node_t *node)
{
    int rc = devfs_register_node(node);

    if (rc != 0)
        klog_print("DEVFS_INIT_REGISTER_FAIL\n");
    return rc;
}

int devfs_init(void)
{
    if (devfs_register_node_checked(&dev_null) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_zero) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_console) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_tty) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_tty0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_tty1) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_stdin) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_stdout) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_stderr) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_kmsg) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_audio) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_mouse) != 0)
        return -1;
    devfs_input_init();
    if (devfs_register_node_checked(&dev_net) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_disk) != 0)
        return -1;
    devfs_register_disk_topology();
    if (devfs_register_node_checked(&dev_random) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_urandom) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_full) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_fb0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_events0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_input_event0) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_serial) != 0)
        return -1;
    if (devfs_register_node_checked(&dev_ptmx) != 0)
        return -1;
    devfs_pty_register_nodes();
    if (devfs_register_node_checked(&dev_ipc) != 0)
        return -1;
#if CONFIG_ENABLE_BLUETOOTH
    if (devfs_register_node_checked(&dev_bluetooth_hci0) != 0)
        return -1;
#endif
    ktm_userdev_register();

    return 0;
}

int64_t devfs_open_node(devfs_node_t *node, int flags)
{
    int64_t rc;

    if (!node)
        return -EINVAL;

    if (node->ops && node->ops->open)
    {
        rc = node->ops->open(&node->entry, flags);
        if (rc < 0)
            return rc;
    }

    {
        unsigned long irq_flags = irq_save();

        node->ref_count++;
        irq_restore(irq_flags);
    }
    return 0;
}

int64_t devfs_close_node(devfs_node_t *node)
{
    int64_t rc;
    unsigned long irq_flags;
    int ref_after;

    if (!node)
        return -EINVAL;

    irq_flags = irq_save();
    if (node->ref_count == 0)
    {
        irq_restore(irq_flags);
        return -EBADF;
    }

    node->ref_count--;
    ref_after = node->ref_count;
    irq_restore(irq_flags);

    rc = 0;
    if (node->ops && node->ops->close)
    {
        /*
         * events0: per-fd release (reader divert count).
         * Other devices: last-close teardown only.
         */
        if (node == &dev_events0 || node == &dev_input_event0 ||
            ref_after == 0)
            rc = node->ops->close(&node->entry);
    }

    return rc;
}

devfs_node_t *devfs_find_node(const char *path)
{
    if (!path || strncmp(path, "/dev/", 5) != 0)
        return NULL;
    
    const char *name = path + 5;  /* Skip "/dev/" */
    
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (strcmp(dev_nodes[i]->entry.name, name) == 0)
        {
            return dev_nodes[i];
        }
    }
    
    return NULL;
}

int devfs_stat_path(const char *path, stat_t *buf)
{
    devfs_node_t *node;

    if (!path || !buf)
        return -EINVAL;

    if (strcmp(path, "/dev") == 0 || strcmp(path, "/dev/") == 0)
    {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFDIR | 0755;
        buf->st_nlink = 2;
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_blksize = 512;
        return 0;
    }

    if (devfs_is_virtual_subdir(path))
    {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = S_IFDIR | 0755;
        buf->st_nlink = 2;
        buf->st_uid = 0;
        buf->st_gid = 0;
        buf->st_blksize = 512;
        return 0;
    }

    node = devfs_find_node(path);
    if (!node)
        return -ENOENT;

    memset(buf, 0, sizeof(*buf));
    buf->st_mode = S_IFCHR | (node->entry.mode & 0777);
    buf->st_rdev = node->entry.device_id;
    buf->st_nlink = 1;
    buf->st_uid = node->entry.uid;
    buf->st_gid = node->entry.gid;
    buf->st_blksize = 512;
    return 0;
}

int devfs_chown_path(const char *path, uid_t owner, gid_t group)
{
    devfs_node_t *node;

    if (!path)
        return -EINVAL;
    node = devfs_find_node(path);
    if (!node)
        return -ENOENT;
    if (owner != (uid_t)-1)
        node->entry.uid = owner;
    if (group != (gid_t)-1)
        node->entry.gid = group;
    return 0;
}

static const char *devfs_virtual_subdir_prefix(const char *path)
{
    if (!path)
        return NULL;

    if (strcmp(path, "/dev/pts") == 0 || strcmp(path, "/dev/pts/") == 0)
        return "pts";
    if (strcmp(path, "/dev/input") == 0 || strcmp(path, "/dev/input/") == 0)
        return "input";
    if (strcmp(path, "/dev/bluetooth") == 0 ||
        strcmp(path, "/dev/bluetooth/") == 0)
        return "bluetooth";

    return NULL;
}

int devfs_is_virtual_subdir(const char *path)
{
    return devfs_virtual_subdir_prefix(path) != NULL;
}

int devfs_readdir_subdir(const char *path, struct vfs_dirent *entries,
                         int max_entries)
{
    const char *prefix;
    size_t prefix_len;
    int n = 0;
    int i;
    int j;

    prefix = devfs_virtual_subdir_prefix(path);
    if (!prefix || !entries || max_entries <= 0)
        return -EINVAL;

    prefix_len = strlen(prefix);
    memset(entries, 0, (size_t)max_entries * sizeof(struct vfs_dirent));

    strncpy(entries[n].name, ".", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;
    if (n >= max_entries)
        return n;

    strncpy(entries[n].name, "..", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;

    for (i = 0; i < num_dev_nodes && n < max_entries; i++)
    {
        const char *name;
        const char *leaf;
        const char *slash;
        int exists = 0;

        if (!dev_nodes[i] || !dev_nodes[i]->entry.name)
            continue;

        name = dev_nodes[i]->entry.name;
        if (strncmp(name, prefix, prefix_len) != 0 || name[prefix_len] != '/')
            continue;

        leaf = name + prefix_len + 1;
        slash = strchr(leaf, '/');
        if (slash)
            continue;

        for (j = 0; j < n; j++)
        {
            if (strcmp(entries[j].name, leaf) == 0)
            {
                exists = 1;
                break;
            }
        }
        if (exists)
            continue;

        strncpy(entries[n].name, leaf, sizeof(entries[n].name) - 1);
        entries[n].type = DT_UNKNOWN;
        n++;
    }

    return n;
}

int devfs_readdir_root(struct vfs_dirent *entries, int max_entries)
{
    int n = 0;
    int i;
    int j;

    if (!entries || max_entries <= 0)
        return -EINVAL;

    memset(entries, 0, (size_t)max_entries * sizeof(struct vfs_dirent));

    strncpy(entries[n].name, ".", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;
    if (n >= max_entries)
        return n;

    strncpy(entries[n].name, "..", sizeof(entries[n].name) - 1);
    entries[n].type = DT_DIR;
    n++;

    for (i = 0; i < num_dev_nodes && n < max_entries; i++)
    {
        const char *name;
        char top[VFS_PATH_MAX];
        const char *slash;
        size_t len;
        int exists = 0;

        if (!dev_nodes[i] || !dev_nodes[i]->entry.name)
            continue;

        name = dev_nodes[i]->entry.name;
        slash = strchr(name, '/');
        len = slash ? (size_t)(slash - name) : strlen(name);
        if (len == 0 || len >= sizeof(top))
            continue;

        memcpy(top, name, len);
        top[len] = '\0';

        for (j = 0; j < n; j++)
        {
            if (strcmp(entries[j].name, top) == 0)
            {
                exists = 1;
                break;
            }
        }
        if (exists)
            continue;

        strncpy(entries[n].name, top, sizeof(entries[n].name) - 1);
        entries[n].type = slash ? DT_DIR : DT_UNKNOWN;
        n++;
    }

    return n;
}

devfs_node_t *devfs_find_node_by_id(uint32_t device_id)
{
    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (dev_nodes[i] && dev_nodes[i]->entry.device_id == device_id)
        {
            return dev_nodes[i];
        }
    }
    return NULL;
}

devfs_node_t *devfs_find_node_by_rdev(uint32_t rdev)
{
    if (rdev == 0)
        return NULL;

    for (int i = 0; i < num_dev_nodes; i++)
    {
        if (dev_nodes[i] && dev_nodes[i]->entry.rdev == rdev)
            return dev_nodes[i];
    }
    return NULL;
}

