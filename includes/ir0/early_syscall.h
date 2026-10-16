/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

/* Architecture-neutral state passed to staged syscall providers. */
struct early_syscall_context
{
	int *leave_usermode;
	int (*smoke_ok)(void);
};

struct early_syscall_dispatch
{
	struct syscall_handler_table handlers;
	/* 0 = uninitialized, 1 = ready, negative = permanent failure. */
	int state;
};

int early_syscall_dispatch_init(
	struct early_syscall_dispatch *dispatch,
	const struct syscall_context_provider *const *providers,
	unsigned int provider_count);
int64_t early_syscall_dispatch_invoke(
	struct early_syscall_dispatch *dispatch,
	struct early_syscall_context *context,
	const struct syscall_context_provider *const *providers,
	unsigned int provider_count, enum ir0_syscall_id id,
	uint64_t a0, uint64_t a1, uint64_t a2,
	uint64_t a3, uint64_t a4, uint64_t a5);
