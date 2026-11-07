/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200112L
/**
 * IR0 Kernel — host test stubs for kmem symbols used by kernel sources
 * compiled into the host test suite.
 */

#include <stdlib.h>
#include <stddef.h>

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

void *__kmalloc_checked(size_t size, const char *file, int line, const char *caller)
{
	(void)file;
	(void)line;
	(void)caller;
	if (host_kmem_should_fail())
		return NULL;
	return malloc(size);
}

void __kfree_checked(void *ptr, const char *file, int line, const char *caller)
{
	(void)file;
	(void)line;
	(void)caller;
	free(ptr);
}

void *kmalloc_try(size_t size)
{
	if (host_kmem_should_fail())
		return NULL;
	return malloc(size);
}

void *kmalloc_aligned_try(size_t size, size_t alignment)
{
	void *p = NULL;
	size_t align = alignment < sizeof(void *) ? sizeof(void *) : alignment;

	if (host_kmem_should_fail())
		return NULL;
	if (size == 0)
		size = align;
	if (posix_memalign(&p, align, size) != 0)
		return NULL;
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
