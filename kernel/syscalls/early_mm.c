/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Architecture-neutral bounded MM for staged userspace. The current arena
 * is the loaded image's Linux initial brk plus a caller-supplied anonymous
 * mmap window. The policy does not name or special-case any program.
 */

#include <stdint.h>
#include <ir0/early_mm.h>
#include <ir0/early_syscall.h>

#define ENOMEM 12
#define EINVAL 22
#define ENOSYS 38
#define PAGE_SIZE 4096UL
#define PROT_READ  0x1UL
#define PROT_WRITE 0x2UL
#define PROT_EXEC  0x4UL

struct early_mm_arena
{
	uint64_t brk;
	uint64_t mapped_end;
	uint64_t map_bump;
};

static struct early_mm_layout g_layout;
static struct early_mm_arena g_arena;
static int g_active;

void early_mm_activate(const struct early_mm_layout *layout)
{
	if (!layout || layout->heap_base == 0 ||
	    layout->map_limit <= layout->map_base ||
	    layout->heap_base >= layout->map_limit)
	{
		g_active = 0;
		return;
	}
	g_layout = *layout;
	g_arena.brk = layout->heap_base;
	g_arena.mapped_end = layout->heap_base;
	g_arena.map_bump = layout->map_base;
	g_active = 1;
}

static int map_zero_page(const struct early_mm_services *ops, uint64_t page)
{
	if (!ops->map_user_page || !ops->zero_user_page)
		return -ENOSYS;
	if (ops->map_user_page(page, 0) != 0)
		return -ENOMEM;
	ops->zero_user_page(page);
	return 0;
}

static int64_t early_brk(struct early_syscall_context *context, uint64_t request)
{
	uint64_t target_end;
	const struct early_mm_services *ops = context->services->mm;

	if (!g_active)
		return -ENOSYS;
	if (!request || request < g_layout.heap_base ||
	    request > g_layout.map_limit)
		return (int64_t)g_arena.brk;
	if (request > UINT64_MAX - (PAGE_SIZE - 1))
		return (int64_t)g_arena.brk;
	target_end = (request + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	if (target_end > g_layout.map_limit)
		return (int64_t)g_arena.brk;
	while (g_arena.mapped_end < target_end)
	{
		if (map_zero_page(ops, g_arena.mapped_end) != 0)
			return (int64_t)g_arena.brk;
		g_arena.mapped_end += PAGE_SIZE;
	}
	g_arena.brk = request;
	return (int64_t)request;
}

static int64_t early_mmap(struct early_syscall_context *context,
			  uint64_t address, uint64_t length)
{
	uint64_t base, end, page;
	const struct early_mm_services *ops = context->services->mm;

	if (!g_active)
		return -ENOSYS;
	if (!length)
		length = 2 * PAGE_SIZE;
	if (length > UINT64_MAX - (PAGE_SIZE - 1))
		return -ENOMEM;
	length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	base = address ? address & ~(PAGE_SIZE - 1) : g_arena.map_bump;
	if (base < g_layout.map_base || base > g_layout.map_limit ||
	    length > g_layout.map_limit - base)
		return -ENOMEM;
	end = base + length;
	for (page = base; page < end; page += PAGE_SIZE)
	{
		if (map_zero_page(ops, page) != 0)
			return -ENOMEM;
	}
	if (end > g_arena.map_bump)
		g_arena.map_bump = end;
	return (int64_t)base;
}

static int64_t early_mprotect(struct early_syscall_context *context,
			      uint64_t address, uint64_t length,
			      uint64_t protection)
{
	const struct early_mm_services *ops = context->services->mm;

	if (!protection || (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC)))
		return -ENOSYS;
	if (!ops || !ops->protect_user_range)
		return -ENOSYS;
	return ops->protect_user_range(address, length,
				       !!(protection & PROT_WRITE),
				       !!(protection & PROT_EXEC)) == 0 ? 0 : -EINVAL;
}

static int64_t early_mm_syscall(void *opaque, enum ir0_syscall_id id,
				uint64_t a0, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5)
{
	struct early_syscall_context *context = opaque;

	(void)a3;
	(void)a4;
	(void)a5;
	if (!context || !context->services || !context->services->mm)
		return -ENOSYS;
	switch (id)
	{
	case IR0_SYSCALL_BRK:
		return early_brk(context, a0);
	case IR0_SYSCALL_MMAP:
		return early_mmap(context, a0, a1);
	case IR0_SYSCALL_MPROTECT:
		return early_mprotect(context, a0, a1, a2);
	case IR0_SYSCALL_MUNMAP:
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_mm_syscalls[] = {
	IR0_SYSCALL_BRK, IR0_SYSCALL_MMAP, IR0_SYSCALL_MUNMAP,
	IR0_SYSCALL_MPROTECT,
};

const struct syscall_context_provider early_mm_provider = {
	.ids = g_mm_syscalls,
	.count = sizeof(g_mm_syscalls) / sizeof(g_mm_syscalls[0]),
	.handler = early_mm_syscall,
};
