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
#include "mmu_early.h"
#include "rootfs_early.h"
#include "syscall_mm_early.h"
#include "pl011.h"
#include "timer.h"

#include <stdint.h>
#include <ir0/boot_log.h>
#include <ir0/early_syscall.h>
#include <ir0/early_io.h>
#include <ir0/early_process.h>
#include <ir0/early_time.h>
#include <ir0/early_vfs.h>
#include <ir0/syscall_id.h>
#include <ir0/syscall_table.h>

#define ENOSYS 38

void arm64_syscall_reset_busybox_heap(void)
{
	arm64_early_mm_reset_busybox_heap();
}

static struct early_syscall_dispatch g_early_syscall_dispatch;

static const struct syscall_context_provider *const g_early_providers[] = {
	&early_io_provider,
	&arm64_early_mm_provider,
	&early_vfs_provider,
	&early_time_provider,
	&early_process_provider,
};

static int arm64_early_copy_from_user(void *destination, uint64_t source,
				      uint64_t size)
{
	uint8_t *dst = destination;
	const volatile uint8_t *src;
	uint64_t index;

	if (!arm64_mmu_user_buf_ok(source, size))
		return -1;
	src = (const volatile uint8_t *)(uintptr_t)source;
	for (index = 0; index < size; index++)
		dst[index] = src[index];
	return 0;
}

static int arm64_early_copy_to_user(uint64_t destination, const void *source,
				    uint64_t size)
{
	volatile uint8_t *dst;
	const uint8_t *src = source;
	uint64_t index;

	if (!arm64_mmu_user_buf_ok(destination, size))
		return -1;
	dst = (volatile uint8_t *)(uintptr_t)destination;
	for (index = 0; index < size; index++)
		dst[index] = src[index];
	return 0;
}

static uint64_t arm64_early_counter_frequency(void)
{
	return timer_get_frequency();
}

static void arm64_early_cpu_relax(void)
{
	__asm__ volatile("yield" ::: "memory");
}

static void arm64_early_time_note(enum early_syscall_event event)
{
	static const char *const tags[] = {
		[EARLY_SYSCALL_NANOSLEEP_OK] = "ARM64_NANOSLEEP_OK",
		[EARLY_SYSCALL_CLOCK_GETTIME_OK] = "ARM64_CLOCK_GETTIME_OK",
		[EARLY_SYSCALL_GETTIMEOFDAY_OK] = "ARM64_GETTIMEOFDAY_OK",
		[EARLY_SYSCALL_CLOCK_NANOSLEEP_OK] = "ARM64_CLOCK_NANOSLEEP_OK",
	};

	if ((unsigned int)event < sizeof(tags) / sizeof(tags[0]) && tags[event])
		ir0_boot_smoke(tags[event]);
}

static void arm64_early_console_write_observed(const char *bytes, uint64_t size)
{
	if (arm64_musl_mode())
		arm64_musl_note_write(bytes, size);
	if (arm64_busybox_mode())
		arm64_busybox_note_write(bytes, size);
}

static void arm64_early_process_exit_observed(int overall_smoke_ok)
{
	if (arm64_busybox_mode())
		ir0_boot_smoke("ARM64_BUSYBOX_EXIT_OK");
	else if (!arm64_musl_mode())
		ir0_boot_smoke(overall_smoke_ok ? "ARM64_SYSCALL_OK" :
						 "ARM64_SYSCALL_FAIL");
}

static const struct early_syscall_services g_arm64_early_services = {
	.copy_from_user = arm64_early_copy_from_user,
	.copy_to_user = arm64_early_copy_to_user,
	.counter_read = timer_read,
	.counter_frequency = arm64_early_counter_frequency,
	.cpu_relax = arm64_early_cpu_relax,
	.note = arm64_early_time_note,
	.console_putc = pl011_putc,
	.console_write_observed = arm64_early_console_write_observed,
	.vfs_read = arm64_rootfs_read,
	.vfs_close = arm64_rootfs_close,
	.vfs_openat = arm64_rootfs_openat,
	.vfs_faccessat = arm64_rootfs_faccessat,
	.vfs_newfstatat = arm64_rootfs_newfstatat,
	.vfs_fstat = arm64_rootfs_fstat,
	.vfs_readlinkat = arm64_rootfs_readlinkat,
	.uts_machine = "aarch64",
	.process_exit_observed = arm64_early_process_exit_observed,
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
	return early_process_smoke_ok() && early_io_smoke_ok() &&
	       early_time_smoke_ok();
}

int64_t arm64_syscall_early(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
			    uint64_t a3, uint64_t a4, uint64_t a5, int *leave_el0)
{
	struct early_syscall_context context;
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

	context.leave_usermode = leave_el0;
	context.smoke_ok = arm64_syscall_smoke_ok;
	context.services = &g_arm64_early_services;
	return early_syscall_dispatch_invoke(
		&g_early_syscall_dispatch, &context, g_early_providers,
		sizeof(g_early_providers) / sizeof(g_early_providers[0]),
		syscall_id, a0, a1, a2, a3, a4, a5);
}
