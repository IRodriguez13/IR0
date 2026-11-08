/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_internal.h
 * Description: Internal APIs for kernel/process/ ownership split (not a public facade).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/console.h>
#include <ir0/debug_runtime.h>
#include <ir0/devfs.h>
#include <ir0/pseudo_fs.h>
#include <kernel/process.h>
#include <config.h>
#include <ir0/clock.h>
#include <ir0/clock_wait.h>
#include <ir0/mm_struct.h>
#include <ir0/mm.h>
#include <ir0/arch_mm.h>
#include <ir0/files_struct.h>
#include <ir0/sched.h>
#include <ir0/kmem.h>
#include <ir0/pipe.h>
#include <ir0/pipe_fd.h>
#include <ir0/paging.h>
#include <ir0/vfs.h>
#include <ir0/ktm/klog.h>
#include <ir0/video_backend.h>
#include <ir0/permissions.h>
#include <ir0/signals.h>
#include <ir0/oops.h>
#include <string.h>
#include <stddef.h>
#include <ir0/errno.h>
#include <ir0/arch_port.h>
#include <ir0/copy_user.h>
#include <ir0/fcntl.h>
#include <ir0/pmm.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_icmp.h>
#include <kernel/syscalls/process_syscalls.h>
#include <kernel/syscalls/epoll_syscalls.h>
#include <kernel/syscalls/io_syscalls.h>
#include <ir0/ipc.h>

/* Global PID/process-list ownership; distinct from per-process object state. */
uint64_t process_registry_lock(void);
void process_registry_unlock(uint64_t irq_flags);
void process_registry_install_if_empty(process_t *process);

typedef struct
{
	uint64_t mapped_pages;
	uint64_t freed_pages;
	uint64_t missing_pages;
	uint64_t level1_present;
	uint64_t level2_present;
	uint64_t level3_present;
	uint64_t leaf_present;
	uint64_t root_freed;
	uint64_t level1_freed;
	uint64_t level2_freed;
	uint64_t level3_freed;
	uint64_t leaf_freed;
} process_reclaim_stats_t;

static inline uint64_t process_irq_save(void)
{
	return (uint64_t)irq_save();
}

static inline void process_irq_restore(uint64_t flags)
{
	irq_restore((unsigned long)flags);
}

/* fdtable.c */
void process_release_fds(process_t *p, const char *pipe_trace_op);
int process_duplicate_fd_table(process_t *parent, process_t *child);

/* mm.c */
void process_unmap_user_pages_all(address_space_root_t root,
				  process_reclaim_stats_t *stats);
struct mmap_region *process_clone_mmap_list(struct mmap_region *parent_list);
void process_fork_destroy_child_mm(process_t *child);
void process_fork_free_mmap_list(process_t *child);

/* wait.c */
void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
				  int parent_state_before);
void wait_exit_audit_classify_user_frame(const char *tag, process_t *p);
void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status);
void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val, int *status);
