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
	EARLY_SYSCALL_STATX_OK,
	EARLY_SYSCALL_SCM_RIGHTS_OK,
	EARLY_SYSCALL_SPLICE_OK,
};

struct early_user_services
{
	int (*copy_from_user)(void *destination, uint64_t source, uint64_t size);
	int (*copy_to_user)(uint64_t destination, const void *source, uint64_t size);
};

struct early_time_services
{
	uint64_t (*counter_read)(void);
	uint64_t (*counter_frequency)(void);
	void (*cpu_relax)(void);
	void (*note)(enum early_syscall_event event);
};

struct early_console_services
{
	void (*console_putc)(char value);
	void (*console_write_observed)(const char *bytes, uint64_t size);
};

struct early_vfs_services
{
	int64_t (*vfs_read)(int fd, uint64_t buffer, uint64_t count);
	int64_t (*vfs_close)(int fd);
	int64_t (*vfs_openat)(int dirfd, uint64_t path, int flags);
	int64_t (*vfs_faccessat)(int dirfd, uint64_t path, int flags);
	int64_t (*vfs_newfstatat)(int dirfd, uint64_t path, uint64_t statbuf,
				 int flags);
	int64_t (*vfs_fstat)(int fd, uint64_t statbuf);
	int64_t (*vfs_readlinkat)(int dirfd, uint64_t path, uint64_t buffer,
				 uint64_t size);
};

struct early_process_services
{
	const char *uts_machine;
	void (*process_exit_observed)(int overall_smoke_ok);
	void (*clone_reaped)(void);
};

struct early_mm_services
{
	int (*map_user_page)(uint64_t page, int executable);
	void (*zero_user_page)(uint64_t page);
	int (*protect_user_range)(uint64_t address, uint64_t length,
				  int writable, int executable);
};

struct early_syscall_services
{
	const struct early_user_services *user;
	const struct early_time_services *time;
	const struct early_console_services *console;
	const struct early_vfs_services *vfs;
	const struct early_process_services *process;
	const struct early_mm_services *mm;
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
