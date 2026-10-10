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
#include <ir0/utsname.h>

#define EAGAIN 11
#define EFAULT 14
#define ENOSYS 38

static int g_getpid_ok;

static int64_t early_uname(uint64_t output)
{
	struct utsname value;
	volatile uint8_t *destination;
	unsigned int index;

	if (!arm64_mmu_user_buf_ok(output, sizeof(value)))
		return -EFAULT;
	ir0_utsname_init(&value, "IR0", "ir0", "0.0.1", "early", "aarch64");
	destination = (volatile uint8_t *)(uintptr_t)output;
	for (index = 0; index < sizeof(value); index++)
		destination[index] = ((const uint8_t *)&value)[index];
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

static int64_t early_getrandom(uint64_t length)
{
	if (length == 0)
		return 0;
	/*
	 * Never claim deterministic bring-up data is random.  The production
	 * provider must wait for a real entropy source; the freestanding image has
	 * none, so callers can use their normal EAGAIN fallback instead.
	 */
	return -EAGAIN;
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
		return early_getrandom(a1);
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
