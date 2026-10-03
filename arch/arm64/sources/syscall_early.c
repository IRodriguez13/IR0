/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscall_early.c
 * Description: Minimal EL0 SVC — getpid / nanosleep / clock_gettime / write / exit.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "syscall_early.h"
#include "elf_load_early.h"
#include "syscall_mm_early.h"
#include "syscall_time_early.h"
#include "syscall_signal_early.h"
#include "syscall_process_early.h"
#include "syscall_context_early.h"
#include "syscall_vfs_early.h"
#include "mmu_early.h"
#include "pl011.h"

#include <stdint.h>
#include <ir0/syscall_id.h>
#include <ir0/syscall_table.h>

#define EBADF  9
#define EFAULT 14
#define ENOSYS 38

#define WRITE_MAX 256UL

#define ENOTTY 25

void arm64_syscall_reset_busybox_heap(void)
{
	arm64_early_mm_reset_busybox_heap();
}

static int g_write_ok;
static struct syscall_handler_table g_early_syscall_handlers;
static int g_early_syscall_handlers_ready;

static const enum ir0_syscall_id g_early_core_syscalls[] = {
	IR0_SYSCALL_WRITE,
	IR0_SYSCALL_IOCTL,
	IR0_SYSCALL_FCNTL,
	IR0_SYSCALL_DUP,
	IR0_SYSCALL_DUP3,
	IR0_SYSCALL_PPOLL,
};

static int64_t arm64_syscall_early_handle(void *opaque,
					 enum ir0_syscall_id syscall_id,
					 uint64_t a0, uint64_t a1, uint64_t a2,
					 uint64_t a3, uint64_t a4, uint64_t a5);

static const struct syscall_context_provider g_early_core_provider = {
	.ids = g_early_core_syscalls,
	.count = sizeof(g_early_core_syscalls) / sizeof(g_early_core_syscalls[0]),
	.handler = arm64_syscall_early_handle,
};

static const struct syscall_context_provider *const g_early_providers[] = {
	&g_early_core_provider,
	&arm64_early_mm_provider,
	&arm64_early_vfs_provider,
	&arm64_early_time_provider,
	&arm64_early_signal_provider,
	&arm64_early_process_provider,
};

static void pl011_put_hex64(uint64_t v)
{
	static const char hex[] = "0123456789abcdef";
	char buf[17];
	int i;

	for (i = 15; i >= 0; i--)
	{
		buf[i] = hex[v & 0xfUL];
		v >>= 4;
	}
	buf[16] = 0;
	pl011_puts(buf);
}


int arm64_syscall_smoke_ok(void)
{
	return arm64_early_process_smoke_ok() && g_write_ok &&
	       arm64_early_time_smoke_ok();
}

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t len)
{
	const char *p;
	uint64_t i;
	uint64_t n;

	if (fd != 1UL && fd != 2UL)
	{
		return -EBADF;
	}
	if (len == 0)
	{
		return 0;
	}
	n = len > WRITE_MAX ? WRITE_MAX : len;
	if (!arm64_mmu_user_buf_ok(buf, n))
	{
		return -EFAULT;
	}

	p = (const char *)(uintptr_t)buf;
	for (i = 0; i < n; i++)
	{
		char c = p[i];

		if (c == '\n')
		{
			pl011_putc('\r');
		}
		pl011_putc(c);
	}
	if (arm64_musl_mode())
		arm64_musl_note_write(p, n);
	if (arm64_busybox_mode())
		arm64_busybox_note_write(p, n);
	g_write_ok = 1;
	return (int64_t)n;
}

static int64_t arm64_syscall_early_handle(void *opaque,
					 enum ir0_syscall_id syscall_id,
					 uint64_t a0, uint64_t a1, uint64_t a2,
					 uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)opaque;
	(void)a3;
	(void)a4;
	(void)a5;
	switch (syscall_id)
	{
	case IR0_SYSCALL_WRITE:
		return sys_write(a0, a1, a2);
	case IR0_SYSCALL_IOCTL:
		/* isatty / TCGETS → not a tty */
		return -ENOTTY;
	case IR0_SYSCALL_FCNTL:
		return 0;
	case IR0_SYSCALL_GETDENTS64:
		return -ENOSYS;
	case IR0_SYSCALL_DUP:
		return (int64_t)a0;
	case IR0_SYSCALL_DUP3:
		return (int64_t)a1;
	case IR0_SYSCALL_PIPE2:
	case IR0_SYSCALL_CLONE:
	case IR0_SYSCALL_EXECVE:
	case IR0_SYSCALL_WAIT4:
		return -ENOSYS;
	case IR0_SYSCALL_PPOLL:
		return 0;
	case IR0_SYSCALL_RT_SIGRETURN:
	case IR0_SYSCALL_FORK:
		return -ENOSYS;
	case IR0_SYSCALL_UNKNOWN:
		return -ENOSYS;
	}
}

static void arm64_syscall_early_handlers_init(void)
{
	unsigned int i;

	if (g_early_syscall_handlers_ready)
		return;
	syscall_handlers_init(&g_early_syscall_handlers);
	for (i = 0; i < sizeof(g_early_providers) / sizeof(g_early_providers[0]); i++)
		(void)syscall_context_provider_register(&g_early_syscall_handlers,
						g_early_providers[i]);
	g_early_syscall_handlers_ready = 1;
}

int64_t arm64_syscall_early(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
			    uint64_t a3, uint64_t a4, uint64_t a5, int *leave_el0)
{
	struct arm64_early_syscall_context context;
	enum ir0_syscall_id syscall_id;

	if (leave_el0)
		*leave_el0 = 0;
	syscall_id = syscall_decode_number(nr);
	if (syscall_id == IR0_SYSCALL_UNKNOWN)
	{
		if (arm64_busybox_mode())
		{
			pl011_puts("ARM64_BB_ENOSYS_");
			pl011_put_hex64(nr);
			pl011_puts("\n");
		}
		return -ENOSYS;
	}

	arm64_syscall_early_handlers_init();
	context.leave_el0 = leave_el0;
	context.smoke_ok = arm64_syscall_smoke_ok;
	return syscall_handler_invoke(&g_early_syscall_handlers, &context,
				      syscall_id, a0, a1, a2, a3, a4, a5);
}
