/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — host test stubs for kmem symbols used by kernel sources
 * compiled into the host test suite.
 */

#include <stdlib.h>
#include <stddef.h>

void *__kmalloc_checked(size_t size, const char *file, int line, const char *caller)
{
	(void)file;
	(void)line;
	(void)caller;
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
	return malloc(size);
}
