/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_domains.h
 * Description: Ownership APIs for process domains (cred/signals/rel/mm cursors).
 *
 * Layout of process_t remains flat for the ASM FS_BASE contract (offset 0x140).
 * These helpers make clone/init/destroy ownership explicit without nesting
 * fields that would break switch_x64.asm.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/types.h>

struct process;

/*
 * Credentials — value clone; no refcount (not shared across processes today).
 * May not sleep. Returns 0 or -errno.
 */
int process_cred_clone(struct process *dst, const struct process *src);

/*
 * Signal handlers/masks — value clone; pending cleared; saved_context not copied.
 * May not sleep. Returns 0 or -errno.
 */
int process_signals_clone(struct process *dst, const struct process *src);

/*
 * Relationships (ppid/tgid/sid/pgid) + identity fields for a new child pid.
 * May not sleep. Returns 0 or -errno.
 */
int process_rel_init_child(struct process *child, const struct process *parent,
			   pid_t child_pid);

/*
 * Copy VM cursor metadata (heap/stack/mmap_base) without sharing page tables.
 * page_directory / mmap_list / ownership start empty — caller fills later.
 * May not sleep. Returns 0 or -errno.
 */
int process_mm_cursor_clone(struct process *dst, const struct process *src);

/*
 * Copy cwd / root (chroot) / comm / rlimits (session-visible process attributes).
 * May not sleep. Returns 0 or -errno.
 */
int process_session_attrs_clone(struct process *dst, const struct process *src);
