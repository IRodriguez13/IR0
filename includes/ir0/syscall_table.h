/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_table.h
 * Description: Semantic native syscall handler table.
 */

#pragma once

#include <stdint.h>
#include <ir0/syscall_id.h>

typedef int64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t,
				     uint64_t, uint64_t, uint64_t);
typedef int64_t (*syscall_context_handler_t)(void *, enum ir0_syscall_id,
				     uint64_t, uint64_t, uint64_t,
				     uint64_t, uint64_t, uint64_t);

struct syscall_handler_table
{
	syscall_handler_t handlers[IR0_SYSCALL_COUNT];
	syscall_context_handler_t context_handlers[IR0_SYSCALL_COUNT];
};

void syscall_handlers_init(struct syscall_handler_table *table);
int syscall_handler_set(struct syscall_handler_table *table,
			enum ir0_syscall_id id, syscall_handler_t handler);
int syscall_context_handler_set(struct syscall_handler_table *table,
				enum ir0_syscall_id id,
				syscall_context_handler_t handler);
int64_t syscall_handler_invoke(const struct syscall_handler_table *table,
			       void *context, enum ir0_syscall_id id, uint64_t arg1,
			       uint64_t arg2, uint64_t arg3, uint64_t arg4,
			       uint64_t arg5, uint64_t arg6);
