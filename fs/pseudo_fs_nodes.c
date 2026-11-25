/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pseudo_fs_nodes.c
 * Description: Registered /proc endpoints using pseudo_fs_ops_t.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/pseudo_fs.h>
#include <ir0/procfs.h>
#include <ir0/process.h>
#include <ir0/fd_types.h>
#include <config.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <string.h>
#include "procfs_internal.h"

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif


static int g_pseudo_fs_nodes_ready;
static int g_proc_dynamic_registered;

static int64_t pseudo_read_wrap_int(void *ctx, char *buf, size_t count, off_t *offset)
{
    int (*fn)(char *, size_t);

    (void)offset;
    fn = (int (*)(char *, size_t))ctx;
    if (!fn)
        return -EINVAL;
    return fn(buf, count);
}


#if CONFIG_ENABLE_BLUETOOTH
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
    st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_size = 0;
    pseudo_fs_stat_now(st);
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

#endif


typedef struct proc_pid_file_ctx
{
    pid_t pid;
    int kind;
    int fd_num;
    int in_use;
} proc_pid_file_ctx_t;

#define PROC_PID_FILE_STATUS  0
#define PROC_PID_FILE_CMDLINE 1
#define PROC_PID_FILE_STAT    2
#define PROC_PID_FILE_MAPS    3
#define PROC_PID_FILE_STATM   4
#define PROC_PID_FILE_ENVIRON 5
#define PROC_PID_FILE_FD_LINK 6
#define PROC_PID_FILE_OOM_SCORE_ADJ 7
#define PROC_PID_FILE_OOM_SCORE 8
#define PROC_PID_FILE_COMM  9
#define PROC_PID_CTX_MAX      32

static proc_pid_file_ctx_t g_proc_pid_ctx[PROC_PID_CTX_MAX];

static proc_pid_file_ctx_t *proc_pid_file_ctx_alloc(void)
{
    int i;

    for (i = 0; i < PROC_PID_CTX_MAX; i++)
    {
        if (!g_proc_pid_ctx[i].in_use)
            return &g_proc_pid_ctx[i];
    }

    return NULL;
}

static int proc_pid_file_parse_fd_link(const char *path, pid_t *pid_out, int *fd_out)
{
    const char *name;
    const char *fd_slash;
    int fd;

    name = proc_resolve_path(path, pid_out);
    if (!name || strcmp(name, "fd_link") != 0)
        return -ENOENT;

    fd_slash = strstr(path, "/fd/");
    if (!fd_slash)
        return -ENOENT;

    fd = atoi(fd_slash + 4);
    if (fd < 0 || fd >= ir0_fd_span())
        return -EINVAL;

    *fd_out = fd;
    return 0;
}

static int proc_pid_file_match(const char *path, void **out_ctx)
{
    pid_t pid;
    const char *name;
    proc_pid_file_ctx_t *ctx;
    int fd_num;

    if (!out_ctx)
        return -EINVAL;

    if (proc_pid_file_parse_fd_link(path, &pid, &fd_num) == 0)
    {
        if (pid > 0 && !process_find_by_pid(pid))
            return -ENOENT;

        ctx = proc_pid_file_ctx_alloc();
        if (!ctx)
            return -ENFILE;

        ctx->pid = pid;
        ctx->kind = PROC_PID_FILE_FD_LINK;
        ctx->fd_num = fd_num;
        ctx->in_use = 1;
        *out_ctx = ctx;
        return 0;
    }

    name = proc_resolve_path(path, &pid);
    if (!name)
        return -ENOENT;

    if (strcmp(name, "status") != 0 && strcmp(name, "cmdline") != 0 &&
        strcmp(name, "maps") != 0 && strcmp(name, "statm") != 0 &&
        strcmp(name, "environ") != 0 &&
        strcmp(name, "oom_score_adj") != 0 &&
        strcmp(name, "oom_score") != 0 &&
        strcmp(name, "comm") != 0 &&
        !(strcmp(name, "stat") == 0 && pid > 0))
        return -ENOENT;

    if (pid > 0 && !process_find_by_pid(pid))
        return -ENOENT;

    ctx = proc_pid_file_ctx_alloc();
    if (!ctx)
        return -ENFILE;

    ctx->pid = pid;
    ctx->fd_num = -1;
    if (strcmp(name, "cmdline") == 0)
        ctx->kind = PROC_PID_FILE_CMDLINE;
    else if (strcmp(name, "stat") == 0)
        ctx->kind = PROC_PID_FILE_STAT;
    else if (strcmp(name, "maps") == 0)
        ctx->kind = PROC_PID_FILE_MAPS;
    else if (strcmp(name, "statm") == 0)
        ctx->kind = PROC_PID_FILE_STATM;
    else if (strcmp(name, "environ") == 0)
        ctx->kind = PROC_PID_FILE_ENVIRON;
    else if (strcmp(name, "oom_score_adj") == 0)
        ctx->kind = PROC_PID_FILE_OOM_SCORE_ADJ;
    else if (strcmp(name, "oom_score") == 0)
        ctx->kind = PROC_PID_FILE_OOM_SCORE;
    else if (strcmp(name, "comm") == 0)
        ctx->kind = PROC_PID_FILE_COMM;
    else
        ctx->kind = PROC_PID_FILE_STATUS;
    ctx->in_use = 1;
    *out_ctx = ctx;
    return 0;
}

static int64_t proc_pid_file_read(void *ctx, char *buf, size_t count, off_t *offset)
{
    proc_pid_file_ctx_t *file = ctx;

    (void)offset;
    if (!file || !buf)
        return -EINVAL;

    if (file->kind == PROC_PID_FILE_CMDLINE)
        return proc_cmdline_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_STAT)
        return proc_pid_stat_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_MAPS)
        return proc_pid_maps_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_STATM)
        return proc_pid_statm_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_ENVIRON)
        return proc_pid_environ_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_OOM_SCORE_ADJ)
        return proc_pid_oom_score_adj_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_OOM_SCORE)
        return proc_pid_oom_score_read(buf, count, file->pid);
    if (file->kind == PROC_PID_FILE_FD_LINK)
        return proc_pid_fd_link_target_read(buf, count, file->pid, file->fd_num);
    if (file->kind == PROC_PID_FILE_COMM)
        return proc_comm_read(buf, count, file->pid);

    return proc_status_read(buf, count, file->pid);
}

static int64_t proc_pid_file_write(void *ctx, const char *buf, size_t count)
{
    proc_pid_file_ctx_t *file = ctx;

    if (!file || !buf)
        return -EINVAL;
    if (file->kind != PROC_PID_FILE_OOM_SCORE_ADJ)
        return -EACCES;

    return proc_pid_oom_score_adj_write(file->pid, buf, count);
}

static int64_t proc_pid_file_close(void *ctx)
{
    proc_pid_file_ctx_t *file = ctx;

    if (file)
        file->in_use = 0;
    return 0;
}

static int proc_pid_file_stat(void *ctx, stat_t *st)
{
    proc_pid_file_ctx_t *file = ctx;
    uid_t uid;
    gid_t gid;
    int rc;

    if (!file || !st)
        return -EINVAL;

    rc = proc_pid_get_owner(file->pid, &uid, &gid);
    if (rc != 0)
        return rc;

    memset(st, 0, sizeof(*st));
    if (file->kind == PROC_PID_FILE_FD_LINK)
        st->st_mode = S_IFLNK | 0777;
    else if (file->kind == PROC_PID_FILE_ENVIRON)
        st->st_mode = S_IFREG | 0400;
    else if (file->kind == PROC_PID_FILE_OOM_SCORE_ADJ)
        st->st_mode = S_IFREG | 0644;
    else
        st->st_mode = S_IFREG | 0444;
    st->st_nlink = 1;
    st->st_uid = uid;
    st->st_gid = gid;
    st->st_size = 0;
    pseudo_fs_stat_now(st);
    return 0;
}

static const pseudo_fs_ops_t proc_pid_file_ops = {
    .read = proc_pid_file_read,
    .write = proc_pid_file_write,
    .close = proc_pid_file_close,
    .stat = proc_pid_file_stat,
};

static void pseudo_fs_register_proc_dynamic(void)
{
    int rc;

    if (g_proc_dynamic_registered)
        return;

    rc = pseudo_fs_register_dynamic("/proc", proc_pid_file_match, &proc_pid_file_ops);
    if (rc == 0)
        g_proc_dynamic_registered = 1;
}


void pseudo_fs_nodes_register_all(void)
{
    if (g_pseudo_fs_nodes_ready)
        return;

    pseudo_fs_register_proc_dynamic();

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
    pseudo_fs_register("/proc", "cmdline", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_boot_cmdline_read);
    pseudo_fs_register("/proc", "uptime", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_uptime_read);
    pseudo_fs_register("/proc", "stat", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_stat_read);
    pseudo_fs_register("/proc", "meminfo", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_meminfo_read);
    pseudo_fs_register("/proc", "ps", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_ps_read);
    pseudo_fs_register("/proc", "drivers", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_drivers_read);
    pseudo_fs_register("/proc", "loadavg", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_loadavg_read);
    pseudo_fs_register("/proc", "filesystems", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_filesystems_read);
    pseudo_fs_register("/proc", "partitions", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_partitions_read);
    pseudo_fs_register("/proc", "interrupts", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_interrupts_read);
    pseudo_fs_register("/proc", "iomem", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_iomem_read);
    pseudo_fs_register("/proc", "ioports", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_ioports_read);
    pseudo_fs_register("/proc", "modules", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_modules_read);
    pseudo_fs_register("/proc", "timer_list", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_timer_list_read);
    pseudo_fs_register("/proc", "kmsg", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_kmsg_read);
    pseudo_fs_register("/proc", "swaps", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_swaps_read);
    pseudo_fs_register("/proc", "vmstat", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_vmstat_read);
    pseudo_fs_register("/proc", "sys/kernel/osrelease", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_sys_kernel_osrelease_read);
    pseudo_fs_register("/proc", "sys/kernel/pid_max", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_sys_kernel_pid_max_read);
    pseudo_fs_register("/proc", "net/dev", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_dev_read);
    pseudo_fs_register("/proc", "net/route", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_route_read);
    pseudo_fs_register("/proc", "net/tcp", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_tcp_read);
    pseudo_fs_register("/proc", "net/udp", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_udp_read);
    pseudo_fs_register("/proc", "net/raw", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_raw_read);
    pseudo_fs_register("/proc", "net/unix", &proc_static_read_ops,
                       (void *)(uintptr_t)proc_net_unix_read);

#if CONFIG_ENABLE_BLUETOOTH
    pseudo_fs_register("/proc", "bluetooth/devices", &proc_bt_devices_ops,
                       (void *)(uintptr_t)ir0_bt_proc_devices_read);
    pseudo_fs_register("/proc", "bluetooth/scan", &proc_bt_scan_ops,
                       (void *)(uintptr_t)ir0_bt_proc_scan_read);
#endif


    g_pseudo_fs_nodes_ready = 1;
}
