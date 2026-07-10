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
#include <ir0/debug_trap.h>
#include <ir0/debug_runtime.h>
#include <ir0/devfs.h>
#include <ir0/pseudo_fs.h>
#include "../process.h"
#include <config.h>
#include <ir0/clock_wait.h>
#include <ir0/scheduler_api.h>
#include <ir0/kmem.h>
#include <ir0/pipe.h>
#include <ir0/paging.h>
#include <ir0/vfs.h>
#include <ir0/serial_io.h>
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
#include <ir0/fase51_debug.h>
#include <ir0/pmm.h>
#include <ir0/sock_udp.h>
#include "../syscalls/process_syscalls.h"

extern void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
				uint64_t *blocked_readers, uint64_t *blocked_writers);
extern void pipe_wake_all(pipe_t *pipe);
extern int64_t process_close_fd(process_t *proc, int fd);

extern process_t *current_process;
extern process_t *process_list;

typedef struct
{
	uint64_t mapped_pages;
	uint64_t freed_pages;
	uint64_t missing_pages;
	uint64_t pdpt_present;
	uint64_t pd_present;
	uint64_t pt_present;
	uint64_t leaf_present;
	uint64_t pml4_freed;
	uint64_t pdpt_freed;
	uint64_t pd_freed;
	uint64_t pt_freed;
	uint64_t leaf_freed;
} process_reclaim_stats_t;

static inline uint64_t process_irq_save(void)
{
#if defined(__x86_64__) || defined(__i386__)
	uint64_t flags;
	__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
	return flags;
#else
	arch_disable_interrupts();
	return 0;
#endif
}

static inline void process_irq_restore(uint64_t flags)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
	(void)flags;
	arch_enable_interrupts();
#endif
}

/* fdtable.c */
void process_release_fds(process_t *p, const char *pipe_trace_op);
int process_duplicate_fd_table(process_t *parent, process_t *child);

/* mm.c */
void process_unmap_user_pages_all(uint64_t *pml4, process_reclaim_stats_t *stats);
struct mmap_region *process_clone_mmap_list(struct mmap_region *parent_list);
void process_fork_destroy_child_mm(process_t *child);
void process_fork_free_mmap_list(process_t *child);

/* wait.c */
void process_reparent_children(process_t *dying_parent);
void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
				  int parent_state_before);
void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status);
void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val, int *status);
void process_wait_wake_blocked_parent(process_t *parent, process_t *child);

/* core.c — shared with wait status copy */
int process_validate_userspace_buffer(const void *buf, size_t size);

/* exit.c / audit (only linked when FASE40_D_AUDIT) */
#if FASE40_D_AUDIT
void fase40_d_audit_reap_line(const char *stage, process_t *child,
			      pid_t parent_pid, int removed, const char *tag);
void fase40_d_audit_destroy_done(process_t *p,
				 const process_reclaim_stats_t *stats,
				 uint64_t orphan_frames);
#endif

/* signals.c */
int process_signal_is_default_fatal(process_t *p, int sig);
