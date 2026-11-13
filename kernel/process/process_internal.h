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
#include <ir0/process.h>
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

/* wait.c */
void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
				  int parent_state_before);
void wait_exit_audit_classify_user_frame(const char *tag, process_t *p);
void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status);
void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val, int *status);
