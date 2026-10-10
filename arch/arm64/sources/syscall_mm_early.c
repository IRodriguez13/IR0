/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: syscall_mm_early.c
 * Description: Freestanding ARM64 MM syscall provider for bring-up.
 */

#include "syscall_mm_early.h"
#include "elf_load_early.h"
#include "mmu_early.h"

#define ENOSYS 38
#define ENOMEM 12

#ifndef ARM64_MUSL_MMAP_BASE
#define ARM64_MUSL_MMAP_BASE 0x431a0000UL
#endif
#ifndef ARM64_MUSL_MMAP_END
#define ARM64_MUSL_MMAP_END 0x43200000UL
#endif
#ifndef ARM64_BB_BRK_START
#define ARM64_BB_BRK_START 0x44158000UL
#endif
#ifndef ARM64_BB_MMAP_BASE
#define ARM64_BB_MMAP_BASE 0x44200000UL
#endif
#ifndef ARM64_BB_MMAP_END
#define ARM64_BB_MMAP_END 0x44800000UL
#endif

static uint64_t g_musl_brk = ARM64_MUSL_MMAP_BASE;
static uint64_t g_musl_mmap_bump = ARM64_MUSL_MMAP_BASE;
static uint64_t g_bb_brk = ARM64_BB_BRK_START;
static uint64_t g_bb_mmap_bump = ARM64_BB_MMAP_BASE;

static void zero_page(uint64_t page)
{
	volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)page;
	uint64_t i;

	for (i = 0; i < 4096UL; i++)
		p[i] = 0;
}

void arm64_early_mm_reset_busybox_heap(void)
{
	g_bb_brk = ARM64_BB_BRK_START;
	g_bb_mmap_bump = ARM64_BB_MMAP_BASE;
}

static int64_t early_brk(uint64_t request)
{
	uint64_t *current;
	uint64_t base;
	uint64_t end;

	if (arm64_busybox_mode())
	{
		current = &g_bb_brk;
		base = ARM64_BB_BRK_START;
		end = ARM64_BB_MMAP_END;
	}
	else
	{
		current = &g_musl_brk;
		base = ARM64_MUSL_MMAP_BASE;
		end = ARM64_MUSL_MMAP_END;
	}
	if (request == 0 || request < base || request > end)
		return (int64_t)*current;
	while (*current < request)
	{
		uint64_t page = *current & ~(4096UL - 1UL);

		if (arm64_mmu_map_user_page_flags(page, 0) != 0)
			return (int64_t)*current;
		zero_page(page);
		*current += 4096UL;
	}
	*current = request;
	return (int64_t)*current;
}

static int64_t early_mmap(uint64_t address, uint64_t length)
{
	uint64_t page;
	uint64_t base;
	uint64_t *bump;
	uint64_t map_base;
	uint64_t map_limit;
	uint64_t map_end;

	if (length == 0)
		length = 8192UL;
	if (length > UINT64_MAX - 4095UL)
		return -ENOMEM;
	length = (length + 4095UL) & ~4095UL;
	if (arm64_busybox_mode())
	{
		bump = &g_bb_mmap_bump;
		map_base = ARM64_BB_MMAP_BASE;
		map_limit = ARM64_BB_MMAP_END;
	}
	else
	{
		bump = &g_musl_mmap_bump;
		map_base = ARM64_MUSL_MMAP_BASE;
		map_limit = ARM64_MUSL_MMAP_END;
	}
	base = address ? address & ~(4096UL - 1UL) : *bump;
	if (base < map_base || base > map_limit || length > map_limit - base)
		return -ENOMEM;
	map_end = base + length;
	/*
	 * This staged provider has no VMA ownership model or MAP_FIXED decoding.
	 * Constrain both allocator-selected and explicit mappings to the active
	 * userspace arena; accepting an arbitrary identity-mapped DRAM address
	 * would let EL0 remap kernel memory as user accessible.
	 */
	for (page = base; page < map_end; page += 4096UL)
	{
		if (arm64_mmu_map_user_page_flags(page, 0) != 0)
			return -ENOMEM;
		zero_page(page);
	}
	if (map_end > *bump && map_end <= map_limit)
		*bump = map_end;
	return (int64_t)base;
}

static int64_t early_mm_syscall(void *context, enum ir0_syscall_id id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)context;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;

	switch (id)
	{
	case IR0_SYSCALL_BRK:
		return early_brk(a0);
	case IR0_SYSCALL_MMAP:
		return early_mmap(a0, a1);
	case IR0_SYSCALL_MUNMAP:
	case IR0_SYSCALL_MPROTECT:
		return 0;
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_mm_syscalls[] = {
	IR0_SYSCALL_BRK,
	IR0_SYSCALL_MMAP,
	IR0_SYSCALL_MUNMAP,
	IR0_SYSCALL_MPROTECT,
};

const struct syscall_context_provider arm64_early_mm_provider = {
	.ids = g_mm_syscalls,
	.count = sizeof(g_mm_syscalls) / sizeof(g_mm_syscalls[0]),
	.handler = early_mm_syscall,
};
