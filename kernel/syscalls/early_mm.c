/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral bounded MM policy for staged userspace. */

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

static struct early_mm_arena g_primary;
static struct early_mm_arena g_secondary;
static const struct early_mm_layout *g_layout;

static void early_mm_configure(const struct early_mm_layout *layout)
{
	if (!layout || g_layout == layout)
		return;
	g_layout = layout;
	g_primary.brk = layout->primary_heap_base;
	g_primary.mapped_end = layout->primary_heap_base;
	g_primary.map_bump = layout->primary_heap_base;
	g_secondary.brk = layout->secondary_heap_base;
	g_secondary.mapped_end = layout->secondary_heap_base;
	g_secondary.map_bump = layout->secondary_map_base;
}

void early_mm_reset_secondary_heap(const struct early_mm_layout *layout)
{
	early_mm_configure(layout);
	if (!layout)
		return;
	g_secondary.brk = layout->secondary_heap_base;
	g_secondary.mapped_end = layout->secondary_heap_base;
	g_secondary.map_bump = layout->secondary_map_base;
}

static int select_arena(struct early_syscall_context *context,
			struct early_mm_arena **arena, uint64_t *heap_base,
			uint64_t *map_base, uint64_t *limit)
{
	const struct early_mm_services *ops = context && context->services ?
		context->services->mm : 0;
	int secondary;

	if (!ops || !ops->mm_layout || !ops->userspace_secondary_mode)
		return -ENOSYS;
	early_mm_configure(ops->mm_layout);
	secondary = ops->userspace_secondary_mode();
	*arena = secondary ? &g_secondary : &g_primary;
	*heap_base = secondary ? ops->mm_layout->secondary_heap_base :
				       ops->mm_layout->primary_heap_base;
	*map_base = secondary ? ops->mm_layout->secondary_map_base :
				      ops->mm_layout->primary_heap_base;
	*limit = secondary ? ops->mm_layout->secondary_map_limit :
			   ops->mm_layout->primary_map_limit;
	return 0;
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
	struct early_mm_arena *arena;
	uint64_t base, map_base, limit, target_end;
	const struct early_mm_services *ops = context->services->mm;

	if (select_arena(context, &arena, &base, &map_base, &limit) != 0)
		return -ENOSYS;
	(void)map_base;
	if (!request || request < base || request > limit)
		return (int64_t)arena->brk;
	if (request > UINT64_MAX - (PAGE_SIZE - 1))
		return (int64_t)arena->brk;
	target_end = (request + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	if (target_end > limit)
		return (int64_t)arena->brk;
	while (arena->mapped_end < target_end) {
		if (map_zero_page(ops, arena->mapped_end) != 0)
			return (int64_t)arena->brk;
		arena->mapped_end += PAGE_SIZE;
	}
	arena->brk = request;
	return (int64_t)request;
}

static int64_t early_mmap(struct early_syscall_context *context,
			  uint64_t address, uint64_t length)
{
	struct early_mm_arena *arena;
	uint64_t heap_base, map_base, limit, base, end, page;
	const struct early_mm_services *ops = context->services->mm;

	if (select_arena(context, &arena, &heap_base, &map_base, &limit) != 0)
		return -ENOSYS;
	(void)heap_base;
	if (!length)
		length = 2 * PAGE_SIZE;
	if (length > UINT64_MAX - (PAGE_SIZE - 1))
		return -ENOMEM;
	length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	base = address ? address & ~(PAGE_SIZE - 1) : arena->map_bump;
	if (base < map_base || base > limit || length > limit - base)
		return -ENOMEM;
	end = base + length;
	for (page = base; page < end; page += PAGE_SIZE)
		if (map_zero_page(ops, page) != 0)
			return -ENOMEM;
	if (end > arena->map_bump)
		arena->map_bump = end;
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

	(void)a3; (void)a4; (void)a5;
	if (!context || !context->services || !context->services->mm)
		return -ENOSYS;
	switch (id) {
	case IR0_SYSCALL_BRK: return early_brk(context, a0);
	case IR0_SYSCALL_MMAP: return early_mmap(context, a0, a1);
	case IR0_SYSCALL_MPROTECT: return early_mprotect(context, a0, a1, a2);
	case IR0_SYSCALL_MUNMAP:
	default: return -ENOSYS;
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
