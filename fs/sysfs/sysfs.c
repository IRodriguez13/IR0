/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sysfs.c
 * Description: sysfs vfs_fstype mounted at /sys. The node table, dynamic
 *              cpu and net lookup, and the inode/file ops live here.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fs/vfs.h>
#include <ir0/cpu_info.h>
#include <ir0/errno.h>
#include <ir0/open_flags.h>
#include <ir0/power_manag.h>
#include <ir0/pseudo_fs.h>
#include <ir0/stat.h>
#include <ir0/statfs.h>
#include <string.h>

#include "sysfs_internal.h"

#ifndef TEST_HOST
#include <config.h>
#endif

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif

#define SYS_ROOT "/sys"
#define SYS_ROOT_LEN (sizeof(SYS_ROOT) - 1)
#define SYS_CPU_MAX 16
#define SYS_BOUNCE 4096

enum
{
    SYS_RO = 0,
    SYS_HOSTNAME,
    SYS_MAXPROC,
    SYS_PANIC,
    SYS_POWER,
    SYS_CPU,
    SYS_CPU_ONLINE
};

struct sysfs_power
{
    const char *name;
    enum ir0_system_action action;
};

struct sysfs_spec
{
    const char *rel;
    int kind;
    uintptr_t ctx;
};

struct sysfs_cpu_node
{
    char rel[48];
    int online;
    unsigned cpu;
};

static const struct sysfs_power sysfs_reboot = {
    "reboot", IR0_SYSTEM_REBOOT
};
static const struct sysfs_power sysfs_poweroff = {
    "poweroff", IR0_SYSTEM_POWEROFF
};
static const struct sysfs_power sysfs_halt = {
    "halt", IR0_SYSTEM_HALT
};

static const struct sysfs_spec sysfs_static[] = {
#if CONFIG_ENABLE_BLUETOOTH
    { "class/bluetooth/hci0/address", SYS_RO,
      (uintptr_t)ir0_bt_sysfs_hci0_address_read },
    { "class/bluetooth/hci0/state", SYS_RO,
      (uintptr_t)ir0_bt_sysfs_hci0_state_read },
    { "class/bluetooth/topology/neighbors", SYS_RO,
      (uintptr_t)ir0_bt_sysfs_topology_neighbors_read },
    { "class/bluetooth/sessions", SYS_RO,
      (uintptr_t)ir0_bt_sysfs_sessions_read },
#endif
    { "kernel/hostname", SYS_HOSTNAME, 0 },
    { "kernel/version", SYS_RO, (uintptr_t)sys_kernel_version_read_reg },
    { "kernel/osrelease", SYS_RO, (uintptr_t)sys_kernel_osrelease_read_reg },
    { "kernel/build", SYS_RO, (uintptr_t)sys_kernel_build_read_reg },
    { "kernel/features", SYS_RO, (uintptr_t)sys_kernel_features_read_reg },
    { "kernel/mm", SYS_RO, (uintptr_t)sys_kernel_mm_read_reg },
    { "kernel/max_processes", SYS_MAXPROC,
      (uintptr_t)sys_kernel_max_processes_read_reg },
    { "kernel/panic", SYS_PANIC, (uintptr_t)sys_kernel_panic_read_reg },
    { "kernel/reboot", SYS_POWER, (uintptr_t)&sysfs_reboot },
    { "kernel/poweroff", SYS_POWER, (uintptr_t)&sysfs_poweroff },
    { "kernel/halt", SYS_POWER, (uintptr_t)&sysfs_halt },
    { "firmware/ir0/protocol", SYS_RO,
      (uintptr_t)sys_firmware_ir0_protocol_read_reg },
    { "firmware/ir0/bootloader", SYS_RO,
      (uintptr_t)sys_firmware_ir0_bootloader_read_reg },
    { "firmware/ir0/cmdline", SYS_RO,
      (uintptr_t)sys_firmware_ir0_cmdline_read_reg },
    { "firmware/ir0/memory_map", SYS_RO,
      (uintptr_t)sys_firmware_ir0_memory_map_read_reg },
    { "firmware/ir0/framebuffer", SYS_RO,
      (uintptr_t)sys_firmware_ir0_framebuffer_read_reg },
    { "firmware/ir0/acpi_rsdp", SYS_RO,
      (uintptr_t)sys_firmware_ir0_acpi_rsdp_read_reg },
    { "firmware/ir0/dtb", SYS_RO, (uintptr_t)sys_firmware_ir0_dtb_read_reg },
    { "firmware/ir0/boot_detail", SYS_RO,
      (uintptr_t)sys_firmware_ir0_boot_detail_read_reg },
    { "devices/system", SYS_RO, (uintptr_t)sys_devices_system_read_reg },
    { "devices/block", SYS_RO, (uintptr_t)sys_devices_block_read_reg },
    { "console/mode", SYS_RO, (uintptr_t)sys_console_mode_read_reg },
    { "class/net", SYS_RO, (uintptr_t)sys_class_net_list_read_reg },
    { "devices/system/cpu", SYS_RO, (uintptr_t)sys_devices_cpus_read_reg },
    { "devices/system/cpu/online", SYS_RO,
      (uintptr_t)sys_devices_cpu_online_list_read_reg },
    { "devices/system/cpu/possible", SYS_RO,
      (uintptr_t)sys_devices_cpu_possible_list_read_reg },
    { "devices/system/cpu/present", SYS_RO,
      (uintptr_t)sys_devices_cpu_present_list_read_reg },
};

#define SYS_STATIC_COUNT (sizeof(sysfs_static) / sizeof(sysfs_static[0]))

static struct sysfs_cpu_node g_cpu_nodes[SYS_CPU_MAX * 2];
static unsigned g_cpu_node_count;
static int g_nodes_ready;
static char g_bounce[SYS_BOUNCE];

static void sysfs_ensure_nodes(void)
{
    uint32_t cpus;
    unsigned i;

    if (g_nodes_ready)
        return;
    cpus = sys_devices_cpu_count();
    if (cpus > SYS_CPU_MAX)
        cpus = SYS_CPU_MAX;
    g_cpu_node_count = 0;
    for (i = 0; i < cpus; i++)
    {
        struct sysfs_cpu_node *dir = &g_cpu_nodes[g_cpu_node_count++];
        struct sysfs_cpu_node *on = &g_cpu_nodes[g_cpu_node_count++];

        snprintf(dir->rel, sizeof(dir->rel), "devices/system/cpu/cpu%u", i);
        dir->online = 0;
        dir->cpu = i;
        snprintf(on->rel, sizeof(on->rel),
                 "devices/system/cpu/cpu%u/online", i);
        on->online = 1;
        on->cpu = i;
    }
    g_nodes_ready = 1;
}

/* Path below /sys, trailing slashes stripped. "" is /sys itself. */
static int sysfs_rel(const char *path, char *rel, size_t rel_sz)
{
    size_t len;

    if (!path || strncmp(path, SYS_ROOT, SYS_ROOT_LEN) != 0)
        return -ENOENT;
    if (path[SYS_ROOT_LEN] != '\0' && path[SYS_ROOT_LEN] != '/')
        return -ENOENT;
    path += SYS_ROOT_LEN;
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

static int sysfs_full(const char *rel, char *out, size_t out_sz)
{
    int n;

    if (rel[0] == '\0')
        n = snprintf(out, out_sz, SYS_ROOT);
    else
        n = snprintf(out, out_sz, SYS_ROOT "/%s", rel);
    if (n < 0 || (size_t)n >= out_sz)
        return -ENAMETOOLONG;
    return 0;
}

static const struct sysfs_spec *sysfs_spec_exact(const char *rel)
{
    size_t i;

    for (i = 0; i < SYS_STATIC_COUNT; i++)
    {
        if (strcmp(sysfs_static[i].rel, rel) == 0)
            return &sysfs_static[i];
    }
    return NULL;
}

static const struct sysfs_cpu_node *sysfs_cpu_exact(const char *rel)
{
    unsigned i;

    for (i = 0; i < g_cpu_node_count; i++)
    {
        if (strcmp(g_cpu_nodes[i].rel, rel) == 0)
            return &g_cpu_nodes[i];
    }
    return NULL;
}

static int sysfs_table_has_child(const char *rel)
{
    size_t len = strlen(rel);
    size_t i;

    if (rel[0] == '\0')
        return SYS_STATIC_COUNT > 0 || g_cpu_node_count > 0;
    for (i = 0; i < SYS_STATIC_COUNT; i++)
    {
        if (strncmp(sysfs_static[i].rel, rel, len) == 0 &&
            sysfs_static[i].rel[len] == '/')
            return 1;
    }
    for (i = 0; i < g_cpu_node_count; i++)
    {
        if (strncmp(g_cpu_nodes[i].rel, rel, len) == 0 &&
            g_cpu_nodes[i].rel[len] == '/')
            return 1;
    }
    return 0;
}

static int sysfs_has_children(const char *rel)
{
    char full[256];

    if (sysfs_table_has_child(rel))
        return 1;
    if (sysfs_full(rel, full, sizeof(full)) != 0)
        return 0;
    return sys_class_net_path_has_children(full);
}

#if CONFIG_ENABLE_NETWORKING
static int sysfs_net_lookup(const char *full, unsigned *attr, char *ifname,
                            size_t ifname_sz)
{
    const char *rest;
    const char *slash;
    size_t nlen;
    int parsed;

    if (strncmp(full, "/sys/class/net/", 15) != 0)
        return -ENOENT;
    rest = full + 15;
    if (rest[0] == '\0')
        return -ENOENT;
    slash = strchr(rest, '/');
    if (!slash)
    {
        nlen = strlen(rest);
        parsed = SYS_NET_ATTR_DIR;
    }
    else if (slash[1] == '\0')
    {
        return -ENOENT;
    }
    else
    {
        nlen = (size_t)(slash - rest);
        parsed = sys_class_net_parse_attr(slash + 1);
        if (parsed < 0)
            return -ENOENT;
    }
    if (nlen == 0 || nlen >= ifname_sz)
        return -ENOENT;
    memcpy(ifname, rest, nlen);
    ifname[nlen] = '\0';
    if (sys_class_net_find_name(ifname) != 0)
        return -ENOENT;
    *attr = (unsigned)parsed;
    return 0;
}
#endif

enum
{
    HIT_NONE = 0,
    HIT_SPEC,
    HIT_CPU,
    HIT_NET
};

struct sysfs_hit
{
    int kind;
    const struct sysfs_spec *spec;
    const struct sysfs_cpu_node *cpu;
    unsigned net_attr;
    char ifname[16];
};

/* Exact node or a net interface path. Intermediate directories are misses. */
static int sysfs_lookup_exact(const char *path, struct sysfs_hit *hit)
{
    char rel[256];
    int ret;

    memset(hit, 0, sizeof(*hit));
    sysfs_ensure_nodes();
    ret = sysfs_rel(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    if (rel[0] == '\0')
        return -ENOENT;
    hit->spec = sysfs_spec_exact(rel);
    if (hit->spec)
    {
        hit->kind = HIT_SPEC;
        return 0;
    }
    hit->cpu = sysfs_cpu_exact(rel);
    if (hit->cpu)
    {
        hit->kind = HIT_CPU;
        return 0;
    }
#if CONFIG_ENABLE_NETWORKING
    {
        char full[256];

        ret = sysfs_full(rel, full, sizeof(full));
        if (ret != 0)
            return ret;
        ret = sysfs_net_lookup(full, &hit->net_attr, hit->ifname,
                               sizeof(hit->ifname));
        if (ret != 0)
            return ret;
        hit->kind = HIT_NET;
        return 0;
    }
#else
    return -ENOENT;
#endif
}

static void sysfs_fill_dir(stat_t *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFDIR | 0555;
    st->st_nlink = 2;
    pseudo_fs_stat_now(st);
}

static void sysfs_fill_reg(stat_t *st, mode_t mode, off_t size)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | mode;
    st->st_nlink = 1;
    st->st_size = size;
    pseudo_fs_stat_now(st);
}

static int sysfs_hit_stat(const struct sysfs_hit *hit, stat_t *st)
{
    int kind;

    if (hit->kind == HIT_NET)
    {
        if (hit->net_attr == (unsigned)SYS_NET_ATTR_DIR)
            sysfs_fill_dir(st);
        else
            sysfs_fill_reg(st, 0444, 4096);
        return 0;
    }
    if (hit->kind == HIT_CPU)
        kind = hit->cpu->online ? SYS_CPU_ONLINE : SYS_CPU;
    else if (hit->kind == HIT_SPEC)
        kind = hit->spec->kind;
    else
        return -ENOENT;

    switch (kind)
    {
    case SYS_HOSTNAME:
    case SYS_MAXPROC:
    case SYS_CPU_ONLINE:
        sysfs_fill_reg(st, 0664, 4096);
        break;
    case SYS_PANIC:
    case SYS_POWER:
        sysfs_fill_reg(st, 0644, 4096);
        break;
    default:
        sysfs_fill_reg(st, 0444, 0);
        break;
    }
    return 0;
}

int sysfs_exact_stat(const char *path, stat_t *st)
{
    struct sysfs_hit hit;
    int ret;

    if (!st)
        return -EINVAL;
    ret = sysfs_lookup_exact(path, &hit);
    if (ret != 0)
        return ret;
    return sysfs_hit_stat(&hit, st);
}

static int sysfs_stat(const char *path, stat_t *st)
{
    char rel[256];
    int ret;

    if (!st)
        return -EINVAL;
    ret = sysfs_rel(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    sysfs_ensure_nodes();
    ret = sysfs_exact_stat(path, st);
    if (ret == 0)
        return 0;
    if (rel[0] == '\0' || sysfs_has_children(rel))
    {
        sysfs_fill_dir(st);
        return 0;
    }
    return ret;
}

static int sysfs_add_dirent(struct vfs_dirent *entries, int max, int n,
                            const char *name, size_t len, unsigned char type)
{
    int i;

    if (n >= max || len == 0 || len >= sizeof(entries[n].name))
        return n;
    for (i = 0; i < n; i++)
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

static int sysfs_add_below(struct vfs_dirent *entries, int max, int n,
                           const char *rel, const char *dir, size_t len)
{
    const char *name;
    const char *slash;
    size_t nlen;

    if (len == 0)
    {
        name = rel;
    }
    else if (strncmp(rel, dir, len) == 0 && rel[len] == '/')
    {
        name = rel + len + 1;
    }
    else
    {
        return n;
    }
    if (name[0] == '\0')
        return n;
    slash = strchr(name, '/');
    nlen = slash ? (size_t)(slash - name) : strlen(name);
    return sysfs_add_dirent(entries, max, n, name, nlen,
                            slash ? DT_DIR : DT_REG);
}

static int sysfs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
    char rel[256];
    char full[256];
    stat_t st;
    size_t len;
    size_t i;
    int n = 0;
    int ret;

    ret = sysfs_stat(path, &st);
    if (ret != 0)
        return ret;
    if (!S_ISDIR(st.st_mode))
        return -ENOTDIR;
    ret = sysfs_rel(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    len = strlen(rel);
    for (i = 0; i < SYS_STATIC_COUNT; i++)
        n = sysfs_add_below(entries, max, n, sysfs_static[i].rel, rel, len);
    for (i = 0; i < g_cpu_node_count; i++)
        n = sysfs_add_below(entries, max, n, g_cpu_nodes[i].rel, rel, len);
    ret = sysfs_full(rel, full, sizeof(full));
    if (ret != 0)
        return ret;
    return sys_class_net_collect_children(full, entries, max, n);
}

static int sysfs_slice(char *dst, size_t count, size_t *done, off_t offset,
                       int64_t full)
{
    size_t n;

    *done = 0;
    if (full < 0)
        return (int)full;
    if ((uint64_t)full > sizeof(g_bounce))
        return -EIO;
    if (offset < 0)
        return -EINVAL;
    if ((size_t)offset >= (size_t)full)
        return 0;
    n = (size_t)full - (size_t)offset;
    if (n > count)
        n = count;
    if (n > 0)
        memcpy(dst, g_bounce + (size_t)offset, n);
    *done = n;
    return 0;
}

static int64_t sysfs_power_text(const struct sysfs_power *control, char *buf,
                                size_t count)
{
    int len;

    if (!control || !buf || count == 0)
        return -EINVAL;
    len = snprintf(buf, count, "write '%s' to request a coordinated %s\n",
                   control->name, control->name);
    if (len < 0)
        return -EIO;
    if ((size_t)len >= count)
        len = (int)count - 1;
    return len;
}

static int64_t sysfs_generate(const struct sysfs_hit *hit)
{
    int (*ro)(char *, size_t);
    int kind;

    if (hit->kind == HIT_NET)
    {
        if (hit->net_attr == (unsigned)SYS_NET_ATTR_DIR)
            return -EISDIR;
        return sys_class_net_attr_read_named(g_bounce, sizeof(g_bounce),
                                             hit->ifname, hit->net_attr);
    }
    if (hit->kind == HIT_CPU)
    {
        if (hit->cpu->online)
            return sys_devices_cpu_online_read_reg(g_bounce, sizeof(g_bounce),
                                                   hit->cpu->cpu);
        return sys_devices_cpu_read_reg(g_bounce, sizeof(g_bounce),
                                        hit->cpu->cpu);
    }
    kind = hit->spec->kind;
    switch (kind)
    {
    case SYS_HOSTNAME:
        return sys_kernel_hostname_read_reg(g_bounce, sizeof(g_bounce));
    case SYS_MAXPROC:
        return sys_kernel_max_processes_read_reg(g_bounce, sizeof(g_bounce));
    case SYS_PANIC:
        return sys_kernel_panic_read_reg(g_bounce, sizeof(g_bounce));
    case SYS_POWER:
        return sysfs_power_text((const struct sysfs_power *)hit->spec->ctx,
                                g_bounce, sizeof(g_bounce));
    default:
        ro = (int (*)(char *, size_t))hit->spec->ctx;
        if (!ro)
            return -EINVAL;
        return ro(g_bounce, sizeof(g_bounce));
    }
}

static int sysfs_read_hit(const struct sysfs_hit *hit, void *buf, size_t count,
                          size_t *done, off_t offset)
{
    if (hit->kind == HIT_NET && hit->net_attr == (unsigned)SYS_NET_ATTR_DIR)
    {
        *done = 0;
        return -EISDIR;
    }
    return sysfs_slice(buf, count, done, offset, sysfs_generate(hit));
}

int sysfs_exact_read(const char *path, void *buf, size_t count,
                     size_t *bytes_read, off_t offset)
{
    struct sysfs_hit hit;
    int ret;

    if (!bytes_read)
        return -EINVAL;
    *bytes_read = 0;
    if (!buf)
        return -EINVAL;
    if (offset < 0)
        return -EINVAL;
    ret = sysfs_lookup_exact(path, &hit);
    if (ret != 0)
        return ret;
    return sysfs_read_hit(&hit, buf, count, bytes_read, offset);
}

static int sysfs_file_read(const char *path, void *buf, size_t count,
                      size_t *bytes_read, off_t offset)
{
    stat_t st;
    struct sysfs_hit hit;
    int ret;

    if (!bytes_read)
        return -EINVAL;
    *bytes_read = 0;
    if (offset < 0)
        return -EINVAL;
    ret = sysfs_stat(path, &st);
    if (ret != 0)
        return ret;
    if (S_ISDIR(st.st_mode))
        return -EISDIR;
    ret = sysfs_lookup_exact(path, &hit);
    if (ret != 0)
        return ret;
    return sysfs_read_hit(&hit, buf, count, bytes_read, offset);
}

static int sysfs_power_write(const struct sysfs_power *control, const char *buf,
                             size_t count)
{
    size_t len;
    size_t i;

    if (!control || !buf || count == 0)
        return -EINVAL;
    len = strlen(control->name);
    if (count < len || memcmp(buf, control->name, len) != 0)
        return -EINVAL;
    for (i = len; i < count; i++)
    {
        if (buf[i] != '\n' && buf[i] != '\r' &&
            buf[i] != ' ' && buf[i] != '\t')
            return -EINVAL;
    }
    kernel_system_shutdown(control->action);
}

static int sysfs_write_hit(const struct sysfs_hit *hit, const void *buf,
                           size_t count, size_t *done)
{
    const char *text = buf;
    int r;

    *done = 0;
    if (hit->kind == HIT_NET)
        return -EINVAL;
    if (hit->kind == HIT_CPU)
    {
        if (!hit->cpu->online)
            return -EINVAL;
        r = sys_devices_cpu_online_write_reg(hit->cpu->cpu, text, count);
        if (r < 0)
            return r;
        *done = count;
        return 0;
    }
    switch (hit->spec->kind)
    {
    case SYS_HOSTNAME:
        r = sys_kernel_hostname_write_reg(text, count);
        break;
    case SYS_MAXPROC:
        r = sys_kernel_max_processes_write_reg(text, count);
        break;
    case SYS_PANIC:
        r = sys_kernel_panic_write_reg(text, count);
        break;
    case SYS_POWER:
        return sysfs_power_write((const struct sysfs_power *)hit->spec->ctx,
                                 text, count);
    default:
        return -EINVAL;
    }
    if (r < 0)
        return r;
    *done = (size_t)r;
    return 0;
}

int sysfs_exact_write(const char *path, const void *buf, size_t count,
                      size_t *bytes_written, off_t offset)
{
    struct sysfs_hit hit;
    int ret;

    (void)offset;
    if (!bytes_written)
        return -EINVAL;
    *bytes_written = 0;
    if (!buf)
        return -EINVAL;
    ret = sysfs_lookup_exact(path, &hit);
    if (ret != 0)
        return ret;
    return sysfs_write_hit(&hit, buf, count, bytes_written);
}

static int sysfs_file_write(const char *path, const void *buf, size_t count,
                            size_t *bytes_written, off_t offset)
{
    stat_t st;
    int ret;

    if (!bytes_written)
        return -EINVAL;
    *bytes_written = 0;
    ret = sysfs_stat(path, &st);
    if (ret != 0)
        return ret;
    if (S_ISDIR(st.st_mode))
        return -EISDIR;
    return sysfs_exact_write(path, buf, count, bytes_written, offset);
}

/*
 * A directory open without O_DIRECTORY is EISDIR. A file node that still
 * has children is the same on a plain open, and ENOTDIR with O_DIRECTORY,
 * because stat reports the node itself as a regular file.
 */
static int sysfs_file_open(const char *path, int flags)
{
    char rel[256];
    stat_t st;
    int ret;

    ret = sysfs_stat(path, &st);
    if (ret != 0)
        return ret;
    ret = sysfs_rel(path, rel, sizeof(rel));
    if (ret != 0)
        return ret;
    if (S_ISDIR(st.st_mode))
    {
        if (!(flags & IR0_O_DIRECTORY))
            return -EISDIR;
        return 0;
    }
    if (sysfs_has_children(rel))
    {
        if (!(flags & IR0_O_DIRECTORY))
            return -EISDIR;
        return -ENOTDIR;
    }
    if (flags & IR0_O_DIRECTORY)
        return -ENOTDIR;
    return 0;
}

int sysfs_exact_open(const char *path, int flags)
{
    struct sysfs_hit hit;

    (void)flags;
    return sysfs_lookup_exact(path, &hit);
}

/* Generated files have no stored size; O_TRUNC is accepted and changes nothing. */
static int sysfs_truncate(const char *path, size_t length)
{
    stat_t st;
    int ret;

    (void)length;
    ret = sysfs_stat(path, &st);
    if (ret != 0)
        return ret;
    return S_ISDIR(st.st_mode) ? -EISDIR : 0;
}

/* Missing sysfs paths stay ENOENT, including open(O_CREAT). */
static int sysfs_create(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return -ENOENT;
}

static struct vfs_ops sysfs_ops = {
    .stat = sysfs_stat,
    .create = sysfs_create,
    .readdir = sysfs_readdir,
    .open = sysfs_file_open,
    .read = sysfs_file_read,
    .write = sysfs_file_write,
    .truncate = sysfs_truncate,
};

static int sysfs_mount(const char *dev, const char *dir)
{
    (void)dev;
    if (!dir || strcmp(dir, SYS_ROOT) != 0)
        return -EINVAL;
    return 0;
}

static int sysfs_umount(const char *dir)
{
    (void)dir;
    return -EBUSY;
}

static int sysfs_statfs(const char *path, struct ir0_statfs *buf)
{
    (void)path;
    buf->f_type = IR0_SYSFS_MAGIC;
    buf->f_bsize = 4096;
    buf->f_frsize = 4096;
    buf->f_namelen = 255;
    return 0;
}

struct vfs_fstype sysfs_fs_type = {
    .name = "sysfs",
    .fs_flags = FS_NOSUID_DEFAULT | FS_SINGLE_INSTANCE,
    .ops = &sysfs_ops,
    .mount = sysfs_mount,
    .umount = sysfs_umount,
    .statfs = sysfs_statfs,
};
