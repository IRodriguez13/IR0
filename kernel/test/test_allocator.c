/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Tests del allocator/heap (sin proceso)
 * kmalloc/kfree; se ejecutan al boot tras heap_init().
 */

#include "test/ktest_harness.h"
#include <ir0/kmem.h>
#include <string.h>

void ktest_allocator(void)
{
	KTEST_BEGIN("allocator");

	void *p = kmalloc(64);
	KASSERT(p != NULL);
	memset(p, 0xAB, 64);
	kfree(p);

	void *q = kmalloc(128);
	KASSERT(q != NULL);
	kfree(q);

	KTEST_END();
}
