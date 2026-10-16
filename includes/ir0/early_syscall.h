/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

enum early_syscall_event
{
	EARLY_SYSCALL_NANOSLEEP_OK,
	EARLY_SYSCALL_CLOCK_GETTIME_OK,
	EARLY_SYSCALL_GETTIMEOFDAY_OK,
	EARLY_SYSCALL_CLOCK_NANOSLEEP_OK,
};

struct early_syscall_services
{
	int (*copy_from_user)(void *destination, uint64_t source, uint64_t size);
	int (*copy_to_user)(uint64_t destination, const void *source, uint64_t size);
	uint64_t (*counter_read)(void);
	uint64_t (*counter_frequency)(void);
	void (*cpu_relax)(void);
	void (*note)(enum early_syscall_event event);
};

/* Architecture-neutral state passed to staged syscall providers. */
struct early_syscall_context
{
	int *leave_usermode;
	int (*smoke_ok)(void);
	const struct early_syscall_services *services;
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
