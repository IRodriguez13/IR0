/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: procfs.c
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Simple /proc filesystem
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs.c
 * Description: Minimal /proc filesystem - on-demand, no mounting
 */

#include "procfs.h"
#include <ir0/stat.h>
#include <ir0/fcntl.h>
#include <ir0/kmem.h>
#include <mm/pmm.h>
#include <mm/allocator.h>
#include <string.h>
#include <ir0/errno.h>
#include <ir0/net.h>
#include <ir0/driver.h>
#include <kernel/process.h>
#include <ir0/version.h>
#include <ir0/serial_io.h>
#include <ir0/clock.h>
#include <arch/common/arch_portable.h>
#include <ir0/partition.h>
#include <ir0/block_dev.h>
#include <fs/vfs.h>
#include <ir0/validation.h>
#include <mm/paging.h>
#include <kernel/resource_registry.h>
#include <config.h>
#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif
#include <ir0/logging.h>

#define PROC_BUFFER_SIZE           4096    /* Standard proc buffer size */
#define PROC_FD_MAP_SIZE           1000    /* Max file descriptors tracked */
#define PROC_OFFSET_MAP_SIZE       2000    /* procfs 1000-1999 + sysfs 3000-3999 */
#define PROC_LINE_MAX_LEN          256     /* Max line length for parsing */
#define PROC_ESTIMATED_ENTRY_SIZE  256     /* Estimated entry size for formatting */
#define PROC_DEFAULT_FILE_SIZE     1024    /* Default file size for stat */
#define BYTES_PER_KB               1024    /* Bytes per kilobyte */
#define BYTES_PER_SECTOR           512     /* Bytes per disk sector */
#define SECTORS_PER_MB             (2 * 1024)  /* Sectors per megabyte (2*1024*512 = 1MB) */

static pid_t proc_fd_pid_map[1000];
static pid_t proc_fd_owner_pid_map[1000];
static int proc_fd_pid_map_init = 0;
/* Track offsets for /proc and /sys files (proc 1000-1999, sys 3000-3999) */
static off_t proc_fd_offset_map[PROC_OFFSET_MAP_SIZE];
static pid_t proc_fd_offset_owner_map[PROC_OFFSET_MAP_SIZE];
static int proc_fd_offset_map_init = 0;

static void proc_u64_to_dec(uint64_t value, char *out, size_t out_len);

static void proc_fd_pid_map_ensure_init(void)
{
    if (proc_fd_pid_map_init)
        return;
    for (int i = 0; i < (int)(sizeof(proc_fd_pid_map) / sizeof(proc_fd_pid_map[0])); i++)
    {
        proc_fd_pid_map[i] = -1;
        proc_fd_owner_pid_map[i] = -1;
    }
    proc_fd_pid_map_init = 1;
}

static void proc_fd_offset_map_ensure_init(void)
{
    if (proc_fd_offset_map_init)
        return;
    for (int i = 0; i < (int)(sizeof(proc_fd_offset_map) / sizeof(proc_fd_offset_map[0])); i++)
    {
        proc_fd_offset_map[i] = 0;
        proc_fd_offset_owner_map[i] = -1;
    }
    proc_fd_offset_map_init = 1;
}

static void proc_set_pid_for_fd(int fd, pid_t pid)
{
    proc_fd_pid_map_ensure_init();
    int idx = fd - 1000;
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_pid_map) / sizeof(proc_fd_pid_map[0])))
    {
        proc_fd_pid_map[idx] = pid;
        proc_fd_owner_pid_map[idx] = current_process ? current_process->task.pid : 0;
    }
}

static pid_t proc_get_pid_for_fd(int fd)
{
    pid_t owner = current_process ? current_process->task.pid : 0;
    proc_fd_pid_map_ensure_init();
    int idx = fd - 1000;
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_pid_map) / sizeof(proc_fd_pid_map[0])))
    {
        if (proc_fd_owner_pid_map[idx] != owner)
            return -1;
        return proc_fd_pid_map[idx];
    }
    return -1;
}
/**
 * get_memory_usage - Get total memory used by kernel
 *
 * Returns: Total memory used in bytes (physical frames + heap)
 */
static uint64_t get_memory_usage(void)
{
    size_t total_frames = 0;
    size_t used_frames = 0;
    size_t heap_total = 0;
    size_t heap_used = 0;
    uint64_t total_used = 0;
    
    /* Get physical memory statistics */
    pmm_stats(&total_frames, &used_frames, NULL);
    
    /* Get heap allocator statistics */
    alloc_stats(&heap_total, &heap_used, NULL);
    
    /* Calculate total used memory:
     * - Physical frames: used_frames * 4KB per frame
     * - Heap: heap_used bytes
     */
    total_used = ((uint64_t)used_frames * PAGE_SIZE_4KB) + (uint64_t)heap_used;
    
    return total_used;
}

/**
 * get_total_memory - Get total available memory
 *
 * Returns: Total memory available in bytes (physical frames + heap)
 */
static uint64_t get_total_memory(void)
{
    size_t total_frames = 0;
    size_t heap_total = 0;
    uint64_t total = 0;
    
    /* Get physical memory statistics */
    pmm_stats(&total_frames, NULL, NULL);
    
    /* Get heap allocator statistics */
    alloc_stats(&heap_total, NULL, NULL);
    
    /* Calculate total available memory:
     * - Physical frames: total_frames * 4KB per frame
     * - Heap: heap_total bytes
     */
    total = ((uint64_t)total_frames * PAGE_SIZE_4KB) + (uint64_t)heap_total;
    
    return total;
}

/*
 * /proc/ps: raw data only, one line per process, tab-separated.
 * pid\tppid\tstate\tuid\tname (OSDev-style: PID PPID S UID CMD)
 * Frontend (ps) does formatting.
 */
static int proc_ps_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    process_t *p = process_list;
    while (p && off < count - 1)
    {
        const char *state_str;
        switch (p->state)
        {
        case PROCESS_READY:   state_str = "R"; break;
        case PROCESS_RUNNING: state_str = "R"; break;
        case PROCESS_BLOCKED: state_str = "S"; break;
        case PROCESS_ZOMBIE:  state_str = "Z"; break;
        default:              state_str = "?"; break;
        }
        const char *name = p->comm[0] ? p->comm : "(none)";
        int n = snprintf(buf + off, count - off,
                         "%d\t%d\t%s\t%u\t%s\n",
                         (int)p->task.pid, (int)p->ppid, state_str,
                         (unsigned)p->uid, name);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        p = p->next;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/*
 * /proc/netinfo: raw data only, one line per interface, tab-separated.
 * name\tmtu\tflags\tmac (mac as xx:xx:xx:xx:xx:xx)
 * Frontend (e.g. ifconfig/netinfo) does formatting.
 */
static int proc_netinfo_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct net_device *dev = net_get_devices();
    if (!dev)
        return 0;
    size_t off = 0;
    while (dev && off < count - 1)
    {
        char flags[32];
        size_t foff = 0;
        flags[0] = '\0';
        if (dev->flags & IFF_UP) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "UP");
        if (dev->flags & IFF_RUNNING) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sRUNNING", foff ? "," : "");
        if (dev->flags & IFF_BROADCAST) foff += (size_t)snprintf(flags + foff, sizeof(flags) - foff, "%sBROADCAST", foff ? "," : "");
        if (foff == 0) snprintf(flags, sizeof(flags), "-");
        int n = snprintf(buf + off, count - off,
                         "%s\t%u\t%s\t%02x:%02x:%02x:%02x:%02x:%02x\n",
                         dev->name ? dev->name : "", (unsigned)dev->mtu, flags,
                         (unsigned)dev->mac[0], (unsigned)dev->mac[1], (unsigned)dev->mac[2],
                         (unsigned)dev->mac[3], (unsigned)dev->mac[4], (unsigned)dev->mac[5]);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        dev = dev->next;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
#else
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    return 0;
#endif
}

/*
 * /proc/net/dev: Linux-style per-interface summary.
 * Header lines mirror common tools that parse Inter-| / face | columns.
 */
static int proc_net_dev_read(char *buf, size_t count)
{
#if CONFIG_ENABLE_NETWORKING
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    int n = snprintf(buf, count,
                     "Inter-|   Receive                                                |  Transmit\n"
                     " face |   packets    errs                                        |  packets    errs\n");
    if (n < 0)
        return -1;
    if ((size_t)n >= count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    off = (size_t)n;

    struct net_device *dev = net_get_devices();
    while (dev && off < count - 1)
    {
        uint64_t rxp = 0, txp = 0, rxe = 0, txe = 0;
        char rxp_str[24];
        char rxe_str[24];
        char txp_str[24];
        char txe_str[24];

        if (dev->get_stats)
            dev->get_stats(dev, &rxp, &txp, &rxe, &txe);

        proc_u64_to_dec(rxp, rxp_str, sizeof(rxp_str));
        proc_u64_to_dec(rxe, rxe_str, sizeof(rxe_str));
        proc_u64_to_dec(txp, txp_str, sizeof(txp_str));
        proc_u64_to_dec(txe, txe_str, sizeof(txe_str));
        n = snprintf(buf + off, count - off, "  %s: %s %s                                          %s %s\n",
                     (dev->name && dev->name[0] != '\0') ? dev->name : "eth0",
                     rxp_str, rxe_str, txp_str, txe_str);
        if (n < 0)
            return -1;
        if ((size_t)n >= count - off)
        {
            buf[count - 1] = '\0';
            return (int)(count - 1);
        }
        off += (size_t)n;
        dev = dev->next;
    }

    return (int)off;
#else
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    return 0;
#endif
}

static int proc_drivers_read(char *buf, size_t count)
{
    return ir0_driver_list_to_buffer(buf, count);
}

/* Check if path is in /proc */
bool is_proc_path(const char *path)
{
    return path && strncmp(path, "/proc/", 6) == 0;
}

/*
 * Parse /proc path - returns entry name after /proc/, extracts PID if present.
 * Supports OSDev-style /proc/pid/N/status, legacy /proc/N/status, and
 * /proc/self/... mapped to the current process PID (Linux-style).
 */
static const char *proc_parse_path(const char *path, pid_t *pid_out)
{
    if (!is_proc_path(path))
        return NULL;

    const char *walk = path;
    char self_resolved[512];

    /*
     * Map /proc/self/<rest> to /proc/<current_pid>/<rest> so the rest of the
     * parser sees a normal numeric PID path.
     */
    if (strncmp(path, "/proc/self/", 11) == 0)
    {
        if (!current_process)
            return NULL;
        int n = snprintf(self_resolved, sizeof(self_resolved), "/proc/%d/%s",
                         (int)current_process->task.pid, path + 11);
        if (n < 0 || (size_t)n >= sizeof(self_resolved))
            return NULL;
        walk = self_resolved;
    }

    /* Skip "/proc/" prefix */
    const char *after_proc = walk + 6;

    /* Check if it's /proc/status (current process) */
    if (strncmp(after_proc, "status", 6) == 0)
    {
        *pid_out = -1;
        return "status";
    }

    /* OSDev-style: /proc/pid or /proc/pid/N or /proc/pid/N/status */
    if (strncmp(after_proc, "pid", 3) == 0 && (after_proc[3] == '\0' || after_proc[3] == '/'))
    {
        if (after_proc[3] == '\0')
        {
            *pid_out = -1;
            return "pid_dir";
        }
        /* pid/123 or pid/123/status or pid/123/cmdline */
        const char *rest = after_proc + 4;
        char *slash = strchr(rest, '/');
        if (!slash)
        {
            *pid_out = atoi(rest);
            return "pid_subdir";
        }
        char pid_str[16];
        size_t pid_len = (size_t)(slash - rest);
        if (pid_len >= sizeof(pid_str))
            return NULL;
        strncpy(pid_str, rest, pid_len);
        pid_str[pid_len] = '\0';
        *pid_out = atoi(pid_str);
        if (strncmp(slash + 1, "status", 6) == 0)
            return "status";
        if (strncmp(slash + 1, "cmdline", 7) == 0)
            return "cmdline";
        return NULL;
    }

    /* Legacy: /proc/[pid]/status or /proc/[pid]/cmdline */
    char *slash = strchr(after_proc, '/');
    if (slash)
    {
        char pid_str[16];
        size_t pid_len = (size_t)(slash - after_proc);
        if (pid_len < sizeof(pid_str) - 1)
        {
            strncpy(pid_str, after_proc, pid_len);
            pid_str[pid_len] = '\0';
            *pid_out = atoi(pid_str);

            if (strncmp(slash + 1, "status", 6) == 0)
                return "status";
            if (strncmp(slash + 1, "cmdline", 7) == 0)
                return "cmdline";
        }
    }

    /* Regular /proc files */
    *pid_out = -1;
    return after_proc;
}

/* /proc/meminfo: raw data only. One line: total_kb\tfree_kb\tused_kb */
int proc_meminfo_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    uint64_t total = get_total_memory();
    uint64_t used = get_memory_usage();
    uint64_t free = total - used;
    uint64_t total_kb = total / BYTES_PER_KB;
    uint64_t free_kb = free / BYTES_PER_KB;
    uint64_t used_kb = used / BYTES_PER_KB;
    char total_kb_str[24];
    char free_kb_str[24];
    char used_kb_str[24];
    proc_u64_to_dec(total_kb, total_kb_str, sizeof(total_kb_str));
    proc_u64_to_dec(free_kb, free_kb_str, sizeof(free_kb_str));
    proc_u64_to_dec(used_kb, used_kb_str, sizeof(used_kb_str));
    int len = snprintf(buf, count, "%s\t%s\t%s\n", total_kb_str, free_kb_str, used_kb_str);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    buf[len] = '\0';
    return len;
}

/* /proc/[pid]/status: raw data only. One line: name\tstate\tpid\tppid\tuid\tgid */
int proc_status_read(char *buf, size_t count, pid_t pid)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    process_t *proc = (pid == -1) ? current_process : process_find_by_pid(pid);
    if (!proc)
        return 0;
    const char *state_str = "?";
    switch (proc->state) {
        case PROCESS_READY:   state_str = "R"; break;
        case PROCESS_RUNNING: state_str = "R"; break;
        case PROCESS_BLOCKED: state_str = "S"; break;
        case PROCESS_ZOMBIE:  state_str = "Z"; break;
    }
    int len = snprintf(buf, count, "%s\t%s\t%d\t%d\t%d\t%d\n",
                       proc->comm[0] ? proc->comm : "(none)",
                       state_str, (int)proc->task.pid, (int)proc->ppid,
                       (int)proc->uid, (int)proc->gid);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    buf[len] = '\0';
    return len;
}

/* /proc/uptime: raw data only. One line: uptime_sec */
int proc_uptime_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    uint64_t uptime = get_system_time() / 1000;
    char uptime_str[24];
    proc_u64_to_dec(uptime, uptime_str, sizeof(uptime_str));
    int len = snprintf(buf, count, "%s\n", uptime_str);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    buf[len] = '\0';
    return len;
}

/* /proc/version: raw data only. One line: version\tdate\ttime\tuser\thost\tcompiler */
int proc_version_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    int len = snprintf(buf, count, "%s\t%s\t%s\t%s\t%s\t%s\n",
                       IR0_VERSION_STRING, IR0_BUILD_DATE, IR0_BUILD_TIME,
                       IR0_BUILD_USER, IR0_BUILD_HOST, IR0_BUILD_CC);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    buf[len] = '\0';
    return len;
}

/*
 * Build "flags" line from CPUID.1 EDX/ECX (silicon feature bits).
 * Each (bit, name) is appended when the bit is set.
 */
static void proc_cpuinfo_flags_from_cpuid(uint32_t edx, uint32_t ecx, char *out, size_t out_size)
{
    static const struct { uint32_t bit; const char *name; } edx_flags[] = {
        { 0, "fpu" }, { 1, "vme" }, { 2, "de" }, { 3, "pse" }, { 4, "tsc" },
        { 5, "msr" }, { 6, "pae" }, { 7, "mce" }, { 8, "cx8" }, { 9, "apic" },
        { 10, "sep" }, { 11, "mtrr" }, { 12, "pge" }, { 13, "mca" }, { 15, "cmov" },
        { 16, "pat" }, { 17, "pse36" }, { 19, "clflush" }, { 23, "mmx" },
        { 24, "fxsr" }, { 25, "sse" }, { 26, "sse2" }, { 28, "htt" }, { 29, "tm" },
        { 31, "pbe" }
    };
    static const struct { uint32_t bit; const char *name; } ecx_flags[] = {
        { 0, "sse3" }, { 1, "pclmulqdq" }, { 9, "ssse3" }, { 12, "fma" },
        { 13, "cx16" }, { 19, "sse4_1" }, { 20, "sse4_2" }, { 21, "x2apic" },
        { 22, "movbe" }, { 23, "popcnt" }, { 25, "aes" }, { 26, "xsave" },
        { 28, "avx" }, { 29, "f16c" }, { 30, "rdrand" }, { 31, "hypervisor" }
    };
    size_t len = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(edx_flags)/sizeof(edx_flags[0]) && len < out_size - 8; i++)
    {
        if (edx & (1U << edx_flags[i].bit))
        {
            if (len > 0) { out[len++] = ' '; out[len] = '\0'; }
            len += (size_t)snprintf(out + len, out_size - len, "%s", edx_flags[i].name);
        }
    }
    for (size_t i = 0; i < sizeof(ecx_flags)/sizeof(ecx_flags[0]) && len < out_size - 8; i++)
    {
        if (ecx & (1U << ecx_flags[i].bit))
        {
            if (len > 0) { out[len++] = ' '; out[len] = '\0'; }
            len += (size_t)snprintf(out + len, out_size - len, "%s", ecx_flags[i].name);
        }
    }
}

/* Generate /proc/cpuinfo content - all fields from silicon (CPUID) where available */
int proc_cpuinfo_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;

    memset(buf, 0, count);

    uint32_t cpu_id = arch_get_cpu_id();
    uint32_t cpu_count = arch_get_cpu_count();

    char vendor_str[13] = {0};
    if (arch_get_cpu_vendor(vendor_str) < 0)
        strncpy(vendor_str, "Unknown", sizeof(vendor_str) - 1);

    uint32_t family = 0, model = 0, stepping = 0;
    arch_get_cpu_signature(&family, &model, &stepping);

    char model_name[49] = {0};
    if (arch_get_cpu_brand_string(model_name, sizeof(model_name)) < 0)
        strncpy(model_name, arch_get_name() ? arch_get_name() : "Unknown", sizeof(model_name) - 1);

    uint32_t max_leaf = 0;
    arch_get_cpuid_max_leaf(&max_leaf);

    uint32_t feat_edx = 0, feat_ecx = 0;
    arch_get_cpu_feature_bits(&feat_edx, &feat_ecx);

    char flags_buf[512];
    proc_cpuinfo_flags_from_cpuid(feat_edx, feat_ecx, flags_buf, sizeof(flags_buf));

    uint32_t clflush_sz = arch_get_cpu_clflush_size();
    if (clflush_sz == 0)
        clflush_sz = 64;

    uint32_t arch_bits = 64;
#if defined(__i386__)
    arch_bits = 32;
#endif

    /* Raw: one line per field, key\tvalue */
    size_t off = 0;
    char t[64];
    int n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "processor\t%u\n", cpu_id); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "vendor_id\t%s\n", vendor_str); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "cpu family\t%u\nmodel\t%u\n", family, model); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "model name\t%s\nstepping\t%u\n", model_name, stepping); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "siblings\t%u\napicid\t%u\ncpuid level\t%u\n", cpu_count, cpu_id, max_leaf); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "flags\t%s\nclflush size\t%u\n", flags_buf, clflush_sz); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    snprintf(t, sizeof(t), "%ubits physical, %ubits virtual", arch_bits, arch_bits);
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "address sizes\t%s\n", t); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/loadavg: raw data only. One line: load1\tload5\tload15\trunning\ttotal\tlast_pid */
int proc_loadavg_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t running = 0, ready = 0;
    process_t *p = process_list;
    while (p) {
        if (p->state == PROCESS_RUNNING) running++;
        else if (p->state == PROCESS_READY) ready++;
        p = p->next;
    }
    uint32_t load1_x100 = (uint32_t)(running * 100 + ready * 50);
    uint32_t load5_x100 = (uint32_t)(running * 100 + ready * 40);
    uint32_t load15_x100 = (uint32_t)(running * 100 + ready * 30);
    int last_pid = current_process ? (int)current_process->task.pid : 0;
    int len = snprintf(buf, count, "%u.%02u\t%u.%02u\t%u.%02u\t%u\t%u\t%d\n",
                       load1_x100 / 100, load1_x100 % 100,
                       load5_x100 / 100, load5_x100 % 100,
                       load15_x100 / 100, load15_x100 % 100,
                       (unsigned)running, (unsigned)(running + ready), last_pid);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    buf[len] = '\0';
    return len;
}

static void proc_u64_to_dec(uint64_t value, char *out, size_t out_len)
{
    char rev[24];
    size_t idx = 0;
    size_t pos = 0;

    if (!out || out_len == 0)
        return;

    if (value == 0)
    {
        if (out_len >= 2)
        {
            out[0] = '0';
            out[1] = '\0';
        }
        else
        {
            out[0] = '\0';
        }
        return;
    }

    while (value > 0 && idx < sizeof(rev))
    {
        rev[idx++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (idx > 0 && pos + 1 < out_len)
    {
        out[pos++] = rev[--idx];
    }
    out[pos] = '\0';
}

/*
 * Format size in sectors (512B) to string in G or M; *len receives length.
 */
static void proc_format_size(uint64_t sectors, char *out, size_t out_size, int *len)
{
    (void)out_size;
    uint64_t bytes = sectors * 512;
    uint64_t mb = bytes / (BYTES_PER_KB * BYTES_PER_KB);
    uint64_t gb = mb / 1024;
    uint64_t val = (gb > 0) ? gb : mb;
    char *p = out;
    if (val == 0)
        *p++ = '0';
    else
    {
        char rev[24];
        int idx = 0;
        while (val > 0) { rev[idx++] = '0' + (val % 10); val /= 10; }
        while (idx > 0) *p++ = rev[--idx];
    }
    *p++ = (gb > 0) ? 'G' : 'M';
    *p = '\0';
    *len = (int)(p - out);
}

/*
 * /proc/blockdevices: raw data (tab-separated).
 * One line per device: type\tname\tmaj\tmin\tsectors\tsize_human\tmodel\tserial
 * type = "disk" | "part". size_human = human-readable (e.g. 128M, 1G).
 */
int proc_blockdevices_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    for (uint8_t i = 0; i < 4; i++)
    {
        const char *disk_name = block_dev_legacy_name(i);
        if (!disk_name || !block_dev_is_present(disk_name))
            continue;
        uint64_t size = block_dev_get_sector_count(disk_name);
        const char *model = "-";
        const char *serial = "-";
        char name_buf[8];
        char size_human[16];
        int sh_len;
        proc_format_size(size, size_human, sizeof(size_human), &sh_len);
        snprintf(name_buf, sizeof(name_buf), "hd%c", 'a' + (int)i);
        char sectors_str[24];
        proc_u64_to_dec(size, sectors_str, sizeof(sectors_str));
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "disk\t%s\t%u\t%u\t%s\t%s\t%s\t%s\n",
                         name_buf, (unsigned)i, 0u, sectors_str,
                         size_human, model, serial);
        if (n < 0) return -1;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        int part_count = get_partition_count(i);
        for (int part_num = 0; part_num < part_count && off < count; part_num++)
        {
            partition_info_t part_info;
            if (get_partition_info(i, part_num, &part_info) != 0)
                continue;
            char part_name[12];
            char part_size_human[16];
            int psh_len;
            proc_format_size(part_info.total_sectors, part_size_human, sizeof(part_size_human), &psh_len);
            snprintf(part_name, sizeof(part_name), "hd%c%d", 'a' + (int)i, part_num + 1);
            char part_sectors_str[24];
            proc_u64_to_dec(part_info.total_sectors, part_sectors_str, sizeof(part_sectors_str));
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "part\t%s\t%u\t%u\t%s\t%s\t-\t-\n",
                         part_name, (unsigned)i, (unsigned)(part_num + 1),
                         part_sectors_str, part_size_human);
            if (n < 0) break;
            if (n >= (int)(count - off)) n = (int)(count - off) - 1;
            off += (size_t)n;
        }
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/filesystems: raw data only. One line per fs: type\tname (type=nodev or empty) */
int proc_filesystems_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    const char *lines[] = { "nodev\tproc", "nodev\tdevfs", "nodev\tramfs", "nodev\ttmpfs", "\tminix" };
    for (size_t i = 0; i < sizeof(lines)/sizeof(lines[0]) && off < count; i++)
    {
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0, "%s\n", lines[i]);
        if (n <= 0 || n >= (int)(count - off)) break;
        off += (size_t)n;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/partitions: raw data only. One line per device: major\tminor\tblocks_1k\tname */
int proc_partitions_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    for (uint8_t disk_id = 0; disk_id < MAX_DISKS; disk_id++)
    {
        const char *disk_name = block_dev_legacy_name(disk_id);
        if (!disk_name || !block_dev_is_present(disk_name))
            continue;
        uint64_t disk_blocks_1k = block_dev_get_sector_count(disk_name) / 2;
        char name_buf[16];
        snprintf(name_buf, sizeof(name_buf), "hd%c", 'a' + disk_id);
        char disk_blocks_str[24];
        proc_u64_to_dec(disk_blocks_1k, disk_blocks_str, sizeof(disk_blocks_str));
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%u\t%s\t%s\n",
                         (unsigned)disk_id, 0u, disk_blocks_str, name_buf);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        int part_count = get_partition_count(disk_id);
        for (int part_num = 0; part_num < part_count; part_num++)
        {
            partition_info_t part_info;
            if (get_partition_info(disk_id, part_num, &part_info) != 0)
                continue;
            uint64_t part_blocks_1k = part_info.total_sectors / 2;
            snprintf(name_buf, sizeof(name_buf), "hd%c%d", 'a' + disk_id, part_num + 1);
            char part_blocks_str[24];
            proc_u64_to_dec(part_blocks_1k, part_blocks_str, sizeof(part_blocks_str));
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%u\t%s\t%s\n",
                         (unsigned)disk_id, (unsigned)(part_num + 1),
                         part_blocks_str, name_buf);
            if (n < 0) break;
            if (n >= (int)(count - off)) n = (int)(count - off) - 1;
            off += (size_t)n;
        }
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/mounts: one line per VFS mount — device path fstype rw 0 0 */
int proc_mounts_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;

    for (struct vfs_mount *m = vfs_get_mounts(); m && off < count; m = m->next) {
        const char *dev = (m->dev[0] != '\0') ? m->dev : "none";
        const char *fst = (m->fs && m->fs->name) ? m->fs->name : "unknown";
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%s %s %s rw 0 0\n",
                         dev, m->path, fst);
        if (n <= 0 || (size_t)n >= count - off)
            break;
        off += (size_t)n;
    }
    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

/*
 * Generate /proc/interrupts content from resource registry only.
 * Data comes from drivers that registered their IRQ (silicon/hardware).
 */
struct irq_collect_ctx {
    char *buf;
    size_t count;
    size_t off;
    uint8_t irqs[16];
    const char *names[16];
    int n;
};

static int irq_collect_cb(uint8_t irq, const char *name, void *ctx)
{
    struct irq_collect_ctx *c = (struct irq_collect_ctx *)ctx;
    if (c->n < 16)
    {
        c->irqs[c->n] = irq;
        c->names[c->n] = name;
        c->n++;
    }
    return 0;
}

/* /proc/interrupts: raw data only. One line per IRQ: irq\tname */
int proc_interrupts_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct irq_collect_ctx ctx = { .buf = buf, .count = count, .off = 0, .n = 0 };
    resource_foreach_irq(irq_collect_cb, &ctx);
    size_t off = 0;
    uint8_t order[16];
    for (int i = 0; i < ctx.n; i++) order[i] = (uint8_t)i;
    for (int i = 0; i < ctx.n - 1; i++)
        for (int j = i + 1; j < ctx.n; j++)
            if (ctx.irqs[order[i]] > ctx.irqs[order[j]])
                { uint8_t t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < ctx.n && off < count; i++)
    {
        int idx = order[i];
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%s\n", (unsigned)ctx.irqs[idx], ctx.names[idx]);
        if (n <= 0 || n >= (int)(count - off)) break;
        off += (size_t)n;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/*
 * Generate /proc/iomem content (physical memory map).
 * System RAM size from PMM; no I/O ports here (they belong in /proc/ioports).
 */
/* /proc/iomem: raw data only. One line per region: start\tend\tname */
int proc_iomem_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t total_frames = 0;
    pmm_stats(&total_frames, NULL, NULL);
    uint64_t ram_end = (uint64_t)total_frames * PAGE_SIZE_4KB;
    if (ram_end > 0) ram_end--;
    char ram_end_str[24];
    proc_u64_to_dec(ram_end, ram_end_str, sizeof(ram_end_str));
    int n = snprintf(buf, count, "0\t%s\tSystem RAM\n", ram_end_str);
    if (n < 0) return -1;
    if (n >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    return n;
}

/*
 * Generate /proc/ioports content from resource registry only.
 * Ranges and names come from drivers that registered their ports (silicon/hardware).
 */
struct ioport_ctx {
    char *buf;
    size_t count;
    size_t off;
};

/* Raw: start\tend\tname per line */
static int ioport_cb(uint16_t start, uint16_t end, const char *name, void *ctx)
{
    struct ioport_ctx *c = (struct ioport_ctx *)ctx;
    int n = snprintf(c->buf + c->off, (c->off < c->count) ? (c->count - c->off) : 0,
                     "%u\t%u\t%s\n", (unsigned)start, (unsigned)end, name);
    if (n > 0 && (size_t)n < c->count - c->off) { c->off += (size_t)n; return 0; }
    return 1;
}

int proc_ioports_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct ioport_ctx ctx = { .buf = buf, .count = count, .off = 0 };
    resource_foreach_ioport(ioport_cb, &ctx);
    if (ctx.off < count)
        buf[ctx.off] = '\0';
    return (int)ctx.off;
}

/* /proc/modules: raw data only. Same as drivers: name\tversion\tlang\tstate\tdescription */
int proc_modules_read(char *buf, size_t count)
{
    return ir0_driver_list_to_buffer(buf, count);
}

/* /proc/timer_list: raw data only. One line: timer\tname\tfrequency\ttick_count\tuptime_sec\tuptime_ms */
int proc_timer_list_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    clock_stats_t stats;
    if (clock_get_stats(&stats) != 0)
        return 0;
    const char *timer_name = "Unknown";
    switch (stats.active_timer) {
        case CLOCK_TIMER_NONE: timer_name = "None"; break;
        case CLOCK_TIMER_PIT:  timer_name = "PIT"; break;
        case CLOCK_TIMER_HPET:  timer_name = "HPET"; break;
        case CLOCK_TIMER_LAPIC: timer_name = "LAPIC"; break;
        case CLOCK_TIMER_RTC:  timer_name = "RTC"; break;
    }
    char tick_count_str[24];
    char uptime_sec_str[24];
    proc_u64_to_dec(stats.tick_count, tick_count_str, sizeof(tick_count_str));
    proc_u64_to_dec(stats.uptime_seconds, uptime_sec_str, sizeof(uptime_sec_str));
    int n = snprintf(buf, count, "%s\t%u\t%s\t%s\t%u\n",
                     timer_name, stats.timer_frequency,
                     tick_count_str,
                     uptime_sec_str,
                     stats.uptime_milliseconds);
    if (n < 0) return -1;
    if (n >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    return n;
}

/* Generate /proc/[pid]/cmdline content */
int proc_cmdline_read(char *buf, size_t count, pid_t pid)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    
    /* Initialize buffer to zero */
    memset(buf, 0, count);
    
    process_t *proc = NULL;
    
    if (pid == -1)
    {
        /* Current process */
        proc = current_process;
    } else
    {
        /* Specific process */
        proc = process_find_by_pid(pid);
    }
    
    if (!proc)
    {
        /* No such process */
        return -1;
    }
    
    /* Get command name from process */
    int len = snprintf(buf, count, "%s", proc->comm[0] ? proc->comm : "(none)");
    
    if (len < 0)
        return -1;
    if (len >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    
    buf[len] = '\0';
    return len;
}

/* Get offset for /proc (1000-1999) or /sys (3000-3999) fd */
off_t proc_get_offset(int fd)
{
    pid_t owner = current_process ? current_process->task.pid : 0;
    proc_fd_offset_map_ensure_init();
    int idx = -1;
    if (fd >= 1000 && fd <= 1999)
        idx = fd - 1000;
    else if (fd >= 3000 && fd <= 3999)
        idx = 1000 + (fd - 3000);
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_offset_map) / sizeof(proc_fd_offset_map[0])))
    {
        if (proc_fd_offset_owner_map[idx] != owner)
            return 0;
        return proc_fd_offset_map[idx];
    }
    return 0;
}

/* linux_dirent64 for proc_getdents (matches sys_getdents) */
struct proc_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};
#define PROC_DT_DIR 4
#define PROC_DT_REG 8

/* /proc/pid directory getdents - OSDev-style */
int proc_getdents(int fd, void *dirent_buf, size_t count)
{
    if (!dirent_buf || count == 0)
        return -EINVAL;

    /* fd 1150 = /proc/pid (list PIDs), fd 1151 = /proc/pid/N (list status, cmdline) */
    if (fd == 1150)
    {
        process_t *p = process_list;
        size_t buf_offset = 0;
        int ino = 1;
        while (p && buf_offset + sizeof(struct proc_dirent64) + 16 < count)
        {
            char pid_str[16];
            int n = snprintf(pid_str, sizeof(pid_str), "%d", (int)p->task.pid);
            if (n <= 0 || n >= (int)sizeof(pid_str))
                break;
            size_t name_len = (size_t)n + 1;
            size_t reclen = (sizeof(struct proc_dirent64) + name_len + 7) & ~7;
            if (buf_offset + reclen > count)
                break;
            struct proc_dirent64 *dent = (struct proc_dirent64 *)((char *)dirent_buf + buf_offset);
            dent->d_ino = (uint64_t)p->task.pid;
            dent->d_off = 0;
            dent->d_reclen = (unsigned short)reclen;
            dent->d_type = PROC_DT_DIR;
            memcpy(dent->d_name, pid_str, name_len);
            buf_offset += reclen;
            ino++;
            p = p->next;
        }
        return (int)buf_offset;
    }
    if (fd == 1151)
    {
        pid_t pid = proc_get_pid_for_fd(1151);
        if (pid < 0 || !process_find_by_pid(pid))
            return -ENOENT;
        size_t buf_offset = 0;
        const char *entries[] = { "status", "cmdline", NULL };
        for (int i = 0; entries[i] && buf_offset + sizeof(struct proc_dirent64) + 16 < count; i++)
        {
            size_t name_len = strlen(entries[i]) + 1;
            size_t reclen = (sizeof(struct proc_dirent64) + name_len + 7) & ~7;
            if (buf_offset + reclen > count)
                break;
            struct proc_dirent64 *dent = (struct proc_dirent64 *)((char *)dirent_buf + buf_offset);
            dent->d_ino = (uint64_t)(i + 1);
            dent->d_off = 0;
            dent->d_reclen = (unsigned short)reclen;
            dent->d_type = PROC_DT_REG;
            memcpy(dent->d_name, entries[i], name_len);
            buf_offset += reclen;
        }
        return (int)buf_offset;
    }
    return -EBADF;
}

/* Set offset for /proc (1000-1999) or /sys (3000-3999) fd */
void proc_set_offset(int fd, off_t offset)
{
    pid_t owner = current_process ? current_process->task.pid : 0;
    proc_fd_offset_map_ensure_init();
    int idx = -1;
    if (fd >= 1000 && fd <= 1999)
        idx = fd - 1000;
    else if (fd >= 3000 && fd <= 3999)
        idx = 1000 + (fd - 3000);
    if (idx >= 0 && idx < (int)(sizeof(proc_fd_offset_map) / sizeof(proc_fd_offset_map[0])))
    {
        proc_fd_offset_map[idx] = offset;
        proc_fd_offset_owner_map[idx] = owner;
    }
}

/* Reset offset for /proc fd (when reopening) */
static void proc_reset_offset(int fd)
{
    proc_set_offset(fd, 0);
}

/* Open /proc file - returns special positive fd, stores PID in fd_table */
int proc_open(const char *path, int flags)
{
    /* Currently read-only: procfs is primarily for reading system information
      * Write support could be added for /proc/sys/ knobs in the future
     */
    (void)flags; /* O_WRONLY, O_RDWR ignored for now; O_DIRECTORY used for pid dirs */

    pid_t pid;
    const char *filename = proc_parse_path(path, &pid);
    if (!filename)
        return -ENOENT;

    /* OSDev-style /proc/pid directory: requires O_DIRECTORY */
    if (strcmp(filename, "pid_dir") == 0)
    {
        if (!(flags & O_DIRECTORY))
            return -ENOTDIR;
        return 1150;  /* proc_getdents will list PIDs */
    }
    if (strcmp(filename, "pid_subdir") == 0)
    {
        if (!(flags & O_DIRECTORY))
            return -ENOTDIR;
        if (!process_find_by_pid(pid))
            return -ENOENT;
        proc_set_pid_for_fd(1151, pid);
        return 1151;  /* proc_getdents will list status, cmdline */
    }

    int fd = -1;
    /* Check if file exists and return special positive fd */
    if (strcmp(filename, "meminfo") == 0)
    {
        fd = 1000;
    } else if (strcmp(filename, "ps") == 0) {
        fd = 1004;
    } else if (strcmp(filename, "netinfo") == 0) {
        fd = 1005;
    } else if (strcmp(filename, "drivers") == 0) {
        fd = 1006;
    } else if (strcmp(filename, "status") == 0)
    {
        proc_set_pid_for_fd(1001, pid);
        fd = 1001;
    } else if (strcmp(filename, "uptime") == 0)
    {
        fd = 1002;
    } else if (strcmp(filename, "version") == 0)
    {
        fd = 1003;
    } else if (strcmp(filename, "cpuinfo") == 0)
    {
        fd = 1007;
    } else if (strcmp(filename, "loadavg") == 0)
    {
        fd = 1008;
    } else if (strcmp(filename, "filesystems") == 0)
    {
        fd = 1009;
    } else if (strcmp(filename, "cmdline") == 0)
    {
        proc_set_pid_for_fd(1010, pid);
        fd = 1010;
    } else if (strcmp(filename, "blockdevices") == 0)
    {
        fd = 1011;
    } else if (strcmp(filename, "partitions") == 0)
    {
        fd = 1012;
    } else if (strcmp(filename, "mounts") == 0)
    {
        fd = 1013;
    } else if (strcmp(filename, "interrupts") == 0)
    {
        fd = 1014;
    } else if (strcmp(filename, "iomem") == 0)
    {
        fd = 1015;
    } else if (strcmp(filename, "ioports") == 0)
    {
        fd = 1016;
    } else if (strcmp(filename, "modules") == 0)
    {
        fd = 1017;
    } else if (strcmp(filename, "timer_list") == 0)
    {
        fd = 1018;
    } else if (strcmp(filename, "kmsg") == 0)
    {
        fd = 1021;
    } else if (strcmp(filename, "swaps") == 0)
    {
        fd = 1022;
    } else if (strcmp(filename, "net/dev") == 0)
    {
        fd = 1023;
    } else
    {
#if CONFIG_ENABLE_BLUETOOTH
        /* Check for bluetooth subdirectory */
        if (strncmp(filename, "bluetooth/", 10) == 0)
        {
            const char *bt_file = filename + 10;
            if (strcmp(bt_file, "devices") == 0)
            {
                fd = 1019;
            }
            else if (strcmp(bt_file, "scan") == 0)
            {
                fd = 1020;
            }
            else
            {
                return -ENOENT;
            }
        }
        else
#endif
        {
            /* File not found */
            return -ENOENT;
        }
    }
    
    /* Reset offset when opening */
    proc_reset_offset(fd);
    return fd;
}

/* Read from /proc file with offset support */
int proc_read(int fd, char *buf, size_t count, off_t offset)
{
    if (!buf || count == 0)
        return 0;
    
    /* Buffer to hold full content - initialize to zero to avoid garbage */
    static char proc_buffer[PROC_BUFFER_SIZE];
    memset(proc_buffer, 0, sizeof(proc_buffer));
    int full_size = 0;
    
    /* Generate full content based on fd */
    switch (fd)
    {
        case 1000:
            full_size = proc_meminfo_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1004:
            full_size = proc_ps_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1005:
            full_size = proc_netinfo_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1006:
            full_size = proc_drivers_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1001:
        {
            pid_t pid = proc_get_pid_for_fd(1001);
            full_size = proc_status_read(proc_buffer, sizeof(proc_buffer), pid);
            break;
        }
        case 1002:
            full_size = proc_uptime_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1003:
            full_size = proc_version_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1007:
            full_size = proc_cpuinfo_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1008:
            full_size = proc_loadavg_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1009:
            full_size = proc_filesystems_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1010:
        {
            pid_t pid = proc_get_pid_for_fd(1010);
            full_size = proc_cmdline_read(proc_buffer, sizeof(proc_buffer), pid);
            break;
        }
        case 1011:
            full_size = proc_blockdevices_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1012:
            full_size = proc_partitions_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1013:
            full_size = proc_mounts_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1014:
            full_size = proc_interrupts_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1015:
            full_size = proc_iomem_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1016:
            full_size = proc_ioports_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1017:
            full_size = proc_modules_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1018:
            full_size = proc_timer_list_read(proc_buffer, sizeof(proc_buffer));
            break;
#if CONFIG_ENABLE_BLUETOOTH
        case 1019:
            /* /proc/bluetooth/devices */
            full_size = ir0_bt_proc_devices_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1020:
            /* /proc/bluetooth/scan */
            full_size = ir0_bt_proc_scan_read(proc_buffer, sizeof(proc_buffer));
            break;
#endif
        case 1022:
            full_size = 0;
            break;
        case 1023:
            full_size = proc_net_dev_read(proc_buffer, sizeof(proc_buffer));
            break;
        case 1021:
            /*
             * /proc/kmsg - kernel log buffer. Dump only to serial, not to VGA.
             * read() returns 0 bytes so dmesg/cat produce no console output.
             */
            full_size = logging_read_buffer(proc_buffer, sizeof(proc_buffer));
            if (full_size > 0 && offset == 0)
            {
                serial_print("\n--- dmesg/kmsg dump ---\n");
                for (int i = 0; i < full_size; i++)
                {
                    if (proc_buffer[i] == '\n')
                        serial_putchar('\r');
                    serial_putchar(proc_buffer[i]);
                }
                serial_print("--- end dmesg/kmsg ---\n");
            }
            full_size = 0;
            break;
        default:
            return -EBADF;
    }
    
    if (full_size < 0)
        return (full_size == -1) ? -EIO : full_size;
    
    /* Ensure full_size doesn't exceed buffer size */
    if (full_size > (int)sizeof(proc_buffer))
        full_size = (int)sizeof(proc_buffer);
    
    /* Ensure null termination */
    if (full_size < (int)sizeof(proc_buffer))
        proc_buffer[full_size] = '\0';
    
    /* If offset is beyond file size, return 0 (EOF) */
    if (offset < 0 || offset >= (off_t)full_size)
        return 0;
    
    /* Calculate how many bytes we can read */
    size_t remaining = (size_t)full_size - (size_t)offset;
    size_t to_read = (count < remaining) ? count : remaining;
    
    /* Safety check: ensure we don't exceed buffer bounds */
    if ((size_t)offset + to_read > sizeof(proc_buffer))
        to_read = sizeof(proc_buffer) - (size_t)offset;
    
    if (to_read == 0)
        return 0;
    
    /* Copy the appropriate portion using memcpy for efficiency and safety */
    memcpy(buf, proc_buffer + (size_t)offset, to_read);
    
    return (int)to_read;
}

/*
 * proc_write - Write to /proc file entry
 *
 * Most /proc nodes are read-only. Writable paths are explicit (e.g.
 * /proc/bluetooth/scan). Writes under /proc/sys/ are not implemented and
 * return -EOPNOTSUPP.
 */
int proc_write(int fd, const char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -EINVAL;

    if (count == 0)
        return 0;

    if (fd < 1000)
        return -EBADF;

    switch (fd)
    {
#if CONFIG_ENABLE_BLUETOOTH
        case 1020:
            /* /proc/bluetooth/scan - Bluetooth scan control */
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

                int result = ir0_bt_proc_scan_write(cmd_buf);
                if (result < 0)
                    return result;
                return (int)count;
            }
#endif
        
        default:
            /*
             * Resolve path from the opener's fd table; writes under the
             * proc/sys virtual tree are not supported (no sysctl knobs in-tree).
             */
            {
                if (!current_process)
                    return -ESRCH;

                if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
                    return -EBADF;

                const char *path = NULL;
                if (current_process->fd_table[fd].in_use)
                    path = current_process->fd_table[fd].path;

                if (!path || strncmp(path, "/proc/", 6) != 0)
                    return -EACCES;

                path += 6;
                if (strncmp(path, "sys/", 4) == 0)
                    return -EOPNOTSUPP;

                return -EACCES;
            }
    }
}

/* Get stat for /proc file */
int proc_stat(const char *path, stat_t *st)
{
    if (!st || !is_proc_path(path))
        return -EINVAL;
    
    pid_t pid;
    const char *filename = proc_parse_path(path, &pid);
    if (!filename)
        return -ENOENT;
    
    /* Check if directory (OSDev-style /proc/pid) */
    if (strcmp(filename, "pid_dir") == 0 || strcmp(filename, "pid_subdir") == 0)
    {
        memset(st, 0, sizeof(stat_t));
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
        st->st_uid = 0;
        st->st_gid = 0;
        st->st_size = 0;
        return 0;
    }

    /*
     * Regular virtual files: keep in sync with proc_open dispatch (same
     * basename / bluetooth subtree as open).
     */
    if (strcmp(filename, "meminfo") == 0 ||
        strcmp(filename, "ps") == 0 ||
        strcmp(filename, "netinfo") == 0 ||
        strcmp(filename, "drivers") == 0 ||
        strcmp(filename, "status") == 0 ||
        strcmp(filename, "uptime") == 0 ||
        strcmp(filename, "version") == 0 ||
        strcmp(filename, "cpuinfo") == 0 ||
        strcmp(filename, "loadavg") == 0 ||
        strcmp(filename, "filesystems") == 0 ||
        strcmp(filename, "cmdline") == 0 ||
        strcmp(filename, "blockdevices") == 0 ||
        strcmp(filename, "partitions") == 0 ||
        strcmp(filename, "mounts") == 0 ||
        strcmp(filename, "interrupts") == 0 ||
        strcmp(filename, "iomem") == 0 ||
        strcmp(filename, "ioports") == 0 ||
        strcmp(filename, "modules") == 0 ||
        strcmp(filename, "timer_list") == 0 ||
        strcmp(filename, "kmsg") == 0 ||
        strcmp(filename, "swaps") == 0 ||
        strcmp(filename, "net/dev") == 0 ||
        (strncmp(filename, "bluetooth/", 10) == 0 &&
         (strcmp(filename + 10, "devices") == 0 ||
          strcmp(filename + 10, "scan") == 0))) {

        memset(st, 0, sizeof(stat_t));
        /* Regular file, read-only */
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
        /* root user */
        st->st_uid = 0;
        /* root group */
        st->st_gid = 0;
        /* Approximate size */
        st->st_size = PROC_DEFAULT_FILE_SIZE;
        
        return 0;
    }
    
    /* File not found */
    return -ENOENT;
}
