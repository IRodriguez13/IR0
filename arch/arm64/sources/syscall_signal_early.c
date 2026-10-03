/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * Minimal ARM64 signal ABI provider used during freestanding bring-up.
 */

#include "syscall_signal_early.h"

#define ENOSYS 38

static int64_t early_signal_syscall(void *context, enum ir0_syscall_id id,
				    uint64_t a0, uint64_t a1, uint64_t a2,
				    uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)context;
	(void)a0;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;

	switch (id) {
	case IR0_SYSCALL_RT_SIGACTION:
	case IR0_SYSCALL_RT_SIGPROCMASK:
		return 0;
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_signal_syscalls[] = {
	IR0_SYSCALL_RT_SIGACTION,
	IR0_SYSCALL_RT_SIGPROCMASK,
};

const struct syscall_context_provider arm64_early_signal_provider = {
	.ids = g_signal_syscalls,
	.count = sizeof(g_signal_syscalls) / sizeof(g_signal_syscalls[0]),
	.handler = early_signal_syscall,
};
