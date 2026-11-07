/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs_internal.h
 * Description: Private shared state and helpers for the split /proc backend.
 *              Not a public ABI; only fs/procfs*.c include this header.
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
#include <ir0/multiboot.h>
#include <ir0/arch_cpu.h>
#include <fs/vfs.h>
#include <ir0/validation.h>
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
