/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs_internal.h
 * Description: Private shared state and helpers for the split /proc backend.
 *              Not a public ABI; only fs/procfs*.c and fs/pseudo_fs_nodes.c
 *              (which binds the readers to their paths) include this header.
 */

#pragma once

#include "procfs.h"
#include <ir0/stat.h>
#include <ir0/fcntl.h>
#include <ir0/kmem.h>
#include <ir0/mm_port.h>
#include <ir0/mman.h>
#include <ir0/permissions.h>
#include <string.h>
#include <ir0/errno.h>
#include <ir0/net.h>
#include <ir0/driver.h>
#include <ir0/klog.h>
#include <ir0/ktm/stack_watch.h>
#include <ir0/process.h>
#include <ir0/mm_struct.h>
#include <ir0/oom.h>
#include <ir0/pmm.h>
#include <ir0/credentials.h>
#include <config.h>
#include <ir0/version.h>
#include <ir0/utsname_info.h>
#include <ir0/clock.h>
#include <ir0/arch_port.h>
#include <ir0/partition.h>
#include <ir0/blockdev.h>
#include <ir0/swap.h>
#include <ir0/boot_memory.h>
#include <ir0/arch_cpu.h>
#include <fs/vfs.h>
#include <ir0/copy_user.h>
#include <ir0/resource_registry.h>
#include <ir0/pseudo_fs.h>
#include <ir0/fd_types.h>
#include <ir0/files_struct.h>
#include <ir0/logging.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_icmp.h>

#define PROC_BUFFER_SIZE           4096
#define PROC_LINE_MAX_LEN          256
#define PROC_ESTIMATED_ENTRY_SIZE  256
#define PROC_DEFAULT_FILE_SIZE     1024
#define IR0_PROC_CONSOLE_TTY_NR    3
#define BYTES_PER_KB               1024
#define BYTES_PER_SECTOR           512
#define SECTORS_PER_MB             (2 * 1024)

typedef struct proc_fd_snap
{
	fd_entry_t table[MAX_FDS_PER_PROCESS];
	int valid;
} proc_fd_snap_t;

int proc_fd_snap_acquire(pid_t pid, proc_fd_snap_t *snap);
const char *proc_parse_path(const char *path, pid_t *pid_out);
void proc_u64_to_dec(uint64_t value, char *out, size_t out_len);

/* /proc file readers, bound to their paths by pseudo_fs_nodes.c. */
int proc_meminfo_read(char *buf, size_t count);
int proc_ps_read(char *buf, size_t count);
int proc_status_read(char *buf, size_t count, pid_t pid);
int proc_cmdline_read(char *buf, size_t count, pid_t pid);
int proc_pid_stat_read(char *buf, size_t count, pid_t pid);
int proc_pid_maps_read(char *buf, size_t count, pid_t pid);
int proc_pid_statm_read(char *buf, size_t count, pid_t pid);
int proc_pid_oom_score_adj_read(char *buf, size_t count, pid_t pid);
int proc_pid_oom_score_adj_write(pid_t pid, const char *buf, size_t count);
int proc_pid_oom_score_read(char *buf, size_t count, pid_t pid);
int proc_pid_fd_link_target_read(char *buf, size_t count, pid_t pid, int fd_num);
int proc_uptime_read(char *buf, size_t count);
int proc_stat_read(char *buf, size_t count);
int proc_version_read(char *buf, size_t count);
int proc_boot_cmdline_read(char *buf, size_t count);
int proc_cpuinfo_read(char *buf, size_t count);
int proc_blockdevices_read(char *buf, size_t count);
int proc_netinfo_read(char *buf, size_t count);
int proc_drivers_read(char *buf, size_t count);
int proc_loadavg_read(char *buf, size_t count);
int proc_filesystems_read(char *buf, size_t count);
int proc_partitions_read(char *buf, size_t count);
int proc_interrupts_read(char *buf, size_t count);
int proc_iomem_read(char *buf, size_t count);
int proc_ioports_read(char *buf, size_t count);
int proc_modules_read(char *buf, size_t count);
int proc_timer_list_read(char *buf, size_t count);
int proc_net_dev_read(char *buf, size_t count);
int proc_net_route_read(char *buf, size_t count);
int proc_net_tcp_read(char *buf, size_t count);
int proc_net_udp_read(char *buf, size_t count);
int proc_net_raw_read(char *buf, size_t count);
int proc_net_unix_read(char *buf, size_t count);
int proc_kmsg_read(char *buf, size_t count);
int proc_swaps_read(char *buf, size_t count);
int proc_mounts_read(char *buf, size_t count);
