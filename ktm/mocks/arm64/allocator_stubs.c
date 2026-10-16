/* SPDX-License-Identifier: GPL-3.0-only */
/** Test-only bump allocator for the minimal ARM64 scheduler boot harness. */

#include <stddef.h>
#include <stdint.h>

static uint8_t g_heap[8192] __attribute__((aligned(16)));
static size_t g_heap_off;

void *__kmalloc_checked(size_t n, const char *file, int line,
			const char *caller)
{
	const size_t align = 16;
	size_t off;

	(void)file;
	(void)line;
	(void)caller;
	off = (g_heap_off + align - 1U) & ~(align - 1U);
	if (off > sizeof(g_heap) || n > sizeof(g_heap) - off)
		return NULL;
	g_heap_off = off + n;
	return &g_heap[off];
}

void __kfree_checked(void *p, const char *file, int line,
			     const char *caller)
{
	(void)p;
	(void)file;
	(void)line;
	(void)caller;
}
