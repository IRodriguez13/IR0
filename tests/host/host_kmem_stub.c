/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200112L
/**
 * IR0 Kernel — host test stubs for kmem symbols used by kernel sources
 * compiled into the host test suite.
 */

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

static long g_allocations_before_failure = -1;

static int host_kmem_should_fail(void)
{
	if (g_allocations_before_failure < 0)
		return 0;
	if (g_allocations_before_failure == 0)
		return 1;
	g_allocations_before_failure--;
	return 0;
}

void host_kmem_fail_after(long successful_allocations)
{
	g_allocations_before_failure = successful_allocations;
}

void __kfree_checked(void *ptr, const char *file, int line, const char *caller)
{
	(void)file;
	(void)line;
	(void)caller;
	free(ptr);
}

/* Mirrors the kernel contract: zero-filled memory, NULL for a zero size. */
void *kmalloc(size_t size)
{
	if (size == 0 || host_kmem_should_fail())
		return NULL;
	return calloc(1, size);
}

void *kcalloc(size_t n, size_t size)
{
	size_t bytes;

	if (__builtin_mul_overflow(n, size, &bytes))
		return NULL;
	return kmalloc(bytes);
}

void *kmalloc_aligned(size_t size, size_t alignment)
{
	void *p = NULL;
	size_t align = alignment < sizeof(void *) ? sizeof(void *) : alignment;

	if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
		return NULL;
	if (host_kmem_should_fail())
		return NULL;
	if (posix_memalign(&p, align, size) != 0)
		return NULL;
	memset(p, 0, size);
	return p;
}

void __kfree_aligned_checked(void *ptr, const char *file, int line,
			     const char *caller)
{
	(void)file;
	(void)line;
	(void)caller;
	free(ptr);
}
