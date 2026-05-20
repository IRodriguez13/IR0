/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pseudo_fs_nodes.c
 * Description: Registered /proc and /sys endpoints using pseudo_fs_ops_t.
 */

#include <ir0/pseudo_fs.h>
#include <ir0/sysfs.h>
#include <ir0/procfs.h>
#include <config.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <string.h>

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif

extern int proc_mounts_read(char *buf, size_t count);

static int g_pseudo_fs_nodes_ready;

static int64_t pseudo_read_wrap_int(void *ctx, char *buf, size_t count, off_t *offset)
{
    int (*fn)(char *, size_t);

    (void)offset;
    fn = (int (*)(char *, size_t))ctx;
    if (!fn)
        return -EINVAL;
    return fn(buf, count);
}

static int64_t pseudo_write_cstr(void *ctx, const char *buf, size_t count)
{
    int (*fn)(const char *);

    fn = (int (*)(const char *))ctx;
    if (!fn || !buf)
        return -EINVAL;

    {
        char cmd_buf[64];
        size_t copy_len = (count < sizeof(cmd_buf) - 1) ? count : (sizeof(cmd_buf) - 1);

        memcpy(cmd_buf, buf, copy_len);
        cmd_buf[copy_len] = '\0';
        while (copy_len > 0 && (cmd_buf[copy_len - 1] == '\n' ||
                                cmd_buf[copy_len - 1] == '\r' ||
                                cmd_buf[copy_len - 1] == ' '))
        {
            copy_len--;
            cmd_buf[copy_len] = '\0';
        }

        {
            int result = fn(cmd_buf);
            if (result < 0)
                return result;
        }
    }

    return (int64_t)count;
}

#if CONFIG_ENABLE_BLUETOOTH
static int64_t pseudo_bt_scan_open(void *ctx, int flags)
{
    (void)ctx;
    (void)flags;
    return ir0_bt_hci_open();
}
#endif

static int pseudo_default_stat(void *ctx, stat_t *st)
{
    (void)ctx;
    if (!st)
        return -EINVAL;
    memset(st, 0, sizeof(*st));
    st->st_mode = 0444;
    st->st_size = 4096;
    return 0;
}

static const pseudo_fs_ops_t proc_mounts_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t proc_static_read_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

#if CONFIG_ENABLE_BLUETOOTH
static const pseudo_fs_ops_t proc_bt_devices_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t proc_bt_scan_ops = {
    .read = pseudo_read_wrap_int,
    .write = pseudo_write_cstr,
    .open = pseudo_bt_scan_open,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_address_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_state_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_neighbors_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};

static const pseudo_fs_ops_t sys_bt_sessions_ops = {
    .read = pseudo_read_wrap_int,
    .stat = pseudo_default_stat,
};
#endif

static int64_t pseudo_hostname_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    (void)ctx;
    (void)offset;
    return sys_kernel_hostname_read_reg(buf, count);
}

static int64_t pseudo_hostname_write(void *ctx, const char *buf, size_t count)
{
    (void)ctx;
    return sys_kernel_hostname_write_reg(buf, count);
}

static const pseudo_fs_ops_t sys_hostname_ops = {
    .read = pseudo_hostname_read,
    .write = pseudo_hostname_write,
    .stat = pseudo_default_stat,
};

void pseudo_fs_nodes_register_all(void)
{
    if (g_pseudo_fs_nodes_ready)
        return;

    pseudo_fs_register("/proc", "mounts", &proc_mounts_ops,
                       (void *)(uintptr_t)proc_mounts_read);
    pseudo_fs_register("/proc", "cpuinfo", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_cpuinfo_read);
    pseudo_fs_register("/proc", "blockdevices", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_blockdevices_read);
    pseudo_fs_register("/proc", "netinfo", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_netinfo_read);
    pseudo_fs_register("/proc", "version", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_version_read);
    pseudo_fs_register("/proc", "uptime", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_uptime_read);

#if CONFIG_ENABLE_BLUETOOTH
    pseudo_fs_register("/proc", "bluetooth/devices", &proc_bt_devices_ops,
                       (void *)(uintptr_t)ir0_bt_proc_devices_read);
    pseudo_fs_register("/proc", "bluetooth/scan", &proc_bt_scan_ops,
                       (void *)(uintptr_t)ir0_bt_proc_scan_read);
    pseudo_fs_register("/sys", "class/bluetooth/hci0/address", &sys_bt_address_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_hci0_address_read);
    pseudo_fs_register("/sys", "class/bluetooth/hci0/state", &sys_bt_state_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_hci0_state_read);
    pseudo_fs_register("/sys", "class/bluetooth/topology/neighbors", &sys_bt_neighbors_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_topology_neighbors_read);
    pseudo_fs_register("/sys", "class/bluetooth/sessions", &sys_bt_sessions_ops,
                       (void *)(uintptr_t)ir0_bt_sysfs_sessions_read);
#endif

    pseudo_fs_register("/sys", "kernel/hostname", &sys_hostname_ops, NULL);

    g_pseudo_fs_nodes_ready = 1;
}
