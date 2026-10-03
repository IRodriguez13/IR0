/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * Freestanding ARM64 descriptor and console I/O provider for bring-up.
 */

#include "syscall_io_early.h"
#include "elf_load_early.h"
#include "mmu_early.h"
#include "pl011.h"

#define EBADF 9
#define EFAULT 14
#define ENOSYS 38
#define ENOTTY 25
#define WRITE_MAX 256UL

static int g_write_ok;

static int64_t early_write(uint64_t descriptor, uint64_t buffer,
			   uint64_t length)
{
	const char *bytes;
	uint64_t count;
	uint64_t index;

	if (descriptor != 1 && descriptor != 2)
		return -EBADF;
	if (length == 0)
		return 0;
	count = length > WRITE_MAX ? WRITE_MAX : length;
	if (!arm64_mmu_user_buf_ok(buffer, count))
		return -EFAULT;
	bytes = (const char *)(uintptr_t)buffer;
	for (index = 0; index < count; index++) {
		if (bytes[index] == '\n')
			pl011_putc('\r');
		pl011_putc(bytes[index]);
	}
	if (arm64_musl_mode())
		arm64_musl_note_write(bytes, count);
	if (arm64_busybox_mode())
		arm64_busybox_note_write(bytes, count);
	g_write_ok = 1;
	return (int64_t)count;
}

static int64_t early_io_syscall(void *context, enum ir0_syscall_id id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)context;
	(void)a3;
	(void)a4;
	(void)a5;
	switch (id) {
	case IR0_SYSCALL_WRITE:
		return early_write(a0, a1, a2);
	case IR0_SYSCALL_IOCTL:
		return -ENOTTY;
	case IR0_SYSCALL_FCNTL:
	case IR0_SYSCALL_PPOLL:
		return 0;
	case IR0_SYSCALL_DUP:
		return (int64_t)a0;
	case IR0_SYSCALL_DUP3:
		return (int64_t)a1;
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_io_syscalls[] = {
	IR0_SYSCALL_WRITE, IR0_SYSCALL_IOCTL, IR0_SYSCALL_FCNTL,
	IR0_SYSCALL_DUP, IR0_SYSCALL_DUP3, IR0_SYSCALL_PPOLL,
};

const struct syscall_context_provider arm64_early_io_provider = {
	.ids = g_io_syscalls,
	.count = sizeof(g_io_syscalls) / sizeof(g_io_syscalls[0]),
	.handler = early_io_syscall,
};

int arm64_early_io_smoke_ok(void)
{
	return g_write_ok;
}
