/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * Freestanding ARM64 process and credential syscall provider for bring-up.
 */

#include "syscall_process_early.h"
#include "syscall_context_early.h"
#include "elf_load_early.h"
#include "mmu_early.h"

#include <ir0/boot_log.h>

#define EFAULT 14
#define ENOSYS 38

static int g_getpid_ok;

static void copy_uname_field(char *destination, const char *source)
{
	unsigned int index;

	for (index = 0; index < 64 && source[index]; index++)
		destination[index] = source[index];
	for (; index < 65; index++)
		destination[index] = 0;
}

static int64_t early_uname(uint64_t output)
{
	char *value;
	unsigned int index;

	if (!arm64_mmu_user_buf_ok(output, 390))
		return -EFAULT;
	value = (char *)(uintptr_t)output;
	for (index = 0; index < 390; index++)
		value[index] = 0;
	copy_uname_field(value + 0, "Linux");
	copy_uname_field(value + 65, "ir0");
	copy_uname_field(value + 130, "0.0.1");
	copy_uname_field(value + 260, "aarch64");
	return 0;
}

static int64_t early_prlimit64(uint64_t old_limit)
{
	uint64_t *limit;

	if (old_limit == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok(old_limit, 16))
		return -EFAULT;
	limit = (uint64_t *)(uintptr_t)old_limit;
	limit[0] = UINT64_MAX;
	limit[1] = UINT64_MAX;
	return 0;
}

static int64_t early_getrandom(uint64_t output, uint64_t length)
{
	uint8_t *bytes;
	uint64_t count = length > 64 ? 64 : length;
	uint64_t index;

	if (count == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok(output, count))
		return -EFAULT;
	bytes = (uint8_t *)(uintptr_t)output;
	for (index = 0; index < count; index++)
		bytes[index] = (uint8_t)(index + 1);
	return (int64_t)count;
}

static int64_t early_exit(struct arm64_early_syscall_context *context,
			  uint64_t status)
{
	if (context && context->leave_el0)
		*context->leave_el0 = 1;
	if (arm64_busybox_mode()) {
		ir0_boot_smoke("ARM64_BUSYBOX_EXIT_OK");
		return (int64_t)status;
	}
	if (arm64_musl_mode())
		return (int64_t)status;
	if (context && context->smoke_ok && context->smoke_ok())
		ir0_boot_smoke("ARM64_SYSCALL_OK");
	else
		ir0_boot_smoke("ARM64_SYSCALL_FAIL");
	return (int64_t)status;
}

static int64_t early_process_syscall(void *opaque, enum ir0_syscall_id id,
				     uint64_t a0, uint64_t a1, uint64_t a2,
				     uint64_t a3, uint64_t a4, uint64_t a5)
{
	struct arm64_early_syscall_context *context = opaque;

	(void)a2;
	(void)a4;
	(void)a5;
	switch (id) {
	case IR0_SYSCALL_GETPID:
		g_getpid_ok = 1;
		return 1;
	case IR0_SYSCALL_GETTID:
	case IR0_SYSCALL_SET_TID_ADDRESS:
	case IR0_SYSCALL_GETPPID:
		return 1;
	case IR0_SYSCALL_GETUID:
	case IR0_SYSCALL_GETEUID:
	case IR0_SYSCALL_GETGID:
	case IR0_SYSCALL_GETEGID:
		return 0;
	case IR0_SYSCALL_UNAME:
		return early_uname(a0);
	case IR0_SYSCALL_SET_ROBUST_LIST:
	case IR0_SYSCALL_PRCTL:
	case IR0_SYSCALL_RSEQ:
		return 0;
	case IR0_SYSCALL_PRLIMIT64:
		return early_prlimit64(a3);
	case IR0_SYSCALL_GETRANDOM:
		return early_getrandom(a0, a1);
	case IR0_SYSCALL_EXIT:
	case IR0_SYSCALL_EXIT_GROUP:
		return early_exit(context, a0);
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_process_syscalls[] = {
	IR0_SYSCALL_GETPID, IR0_SYSCALL_GETTID, IR0_SYSCALL_SET_TID_ADDRESS,
	IR0_SYSCALL_GETUID, IR0_SYSCALL_GETEUID, IR0_SYSCALL_GETGID,
	IR0_SYSCALL_GETEGID, IR0_SYSCALL_GETPPID, IR0_SYSCALL_UNAME,
	IR0_SYSCALL_SET_ROBUST_LIST, IR0_SYSCALL_PRCTL, IR0_SYSCALL_PRLIMIT64,
	IR0_SYSCALL_RSEQ, IR0_SYSCALL_GETRANDOM, IR0_SYSCALL_EXIT,
	IR0_SYSCALL_EXIT_GROUP,
};

const struct syscall_context_provider arm64_early_process_provider = {
	.ids = g_process_syscalls,
	.count = sizeof(g_process_syscalls) / sizeof(g_process_syscalls[0]),
	.handler = early_process_syscall,
};

int arm64_early_process_smoke_ok(void)
{
	return g_getpid_ok;
}
