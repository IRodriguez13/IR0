/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_vfs_early.c
 * Description: Freestanding ARM64 VFS syscall provider for bring-up.
 */

#include "syscall_vfs_early.h"
#include "rootfs_early.h"
#include "mmu_early.h"

#define EBADF 9
#define EFAULT 14
#define ENOENT 2
#define ENOSYS 38

static int64_t early_getcwd(uint64_t buffer, uint64_t size)
{
	char *path;

	if (size < 2 || !arm64_mmu_user_buf_ok(buffer, size))
		return -EFAULT;
	path = (char *)(uintptr_t)buffer;
	path[0] = '/';
	path[1] = 0;
	return 2;
}

static int64_t early_vfs_syscall(void *context, enum ir0_syscall_id id,
				 uint64_t a0, uint64_t a1, uint64_t a2,
				 uint64_t a3, uint64_t a4, uint64_t a5)
{
	int64_t result;

	(void)context;
	(void)a4;
	(void)a5;

	switch (id)
	{
	case IR0_SYSCALL_READ:
		result = arm64_rootfs_read((int)a0, a1, a2);
		return result == -EBADF ? 0 : result;
	case IR0_SYSCALL_CLOSE:
		result = arm64_rootfs_close((int)a0);
		return result == -EBADF ? 0 : result;
	case IR0_SYSCALL_OPENAT:
		return arm64_rootfs_openat((int)a0, a1, (int)a2);
	case IR0_SYSCALL_FACCESSAT:
		return arm64_rootfs_faccessat((int)a0, a1, (int)a2);
	case IR0_SYSCALL_NEWFSTATAT:
		return arm64_rootfs_newfstatat((int)a0, a1, a2, (int)a3);
	case IR0_SYSCALL_FSTAT:
		result = arm64_rootfs_fstat((int)a0, a1);
		return result == -EBADF ? -ENOENT : result;
	case IR0_SYSCALL_READLINKAT:
		return arm64_rootfs_readlinkat((int)a0, a1, a2, a3);
	case IR0_SYSCALL_GETCWD:
		return early_getcwd(a0, a1);
	case IR0_SYSCALL_CHDIR:
		return -ENOENT;
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_vfs_syscalls[] = {
	IR0_SYSCALL_READ,
	IR0_SYSCALL_CLOSE,
	IR0_SYSCALL_OPENAT,
	IR0_SYSCALL_FACCESSAT,
	IR0_SYSCALL_NEWFSTATAT,
	IR0_SYSCALL_FSTAT,
	IR0_SYSCALL_READLINKAT,
	IR0_SYSCALL_GETCWD,
	IR0_SYSCALL_CHDIR,
};

const struct syscall_context_provider arm64_early_vfs_provider = {
	.ids = g_vfs_syscalls,
	.count = sizeof(g_vfs_syscalls) / sizeof(g_vfs_syscalls[0]),
	.handler = early_vfs_syscall,
};
