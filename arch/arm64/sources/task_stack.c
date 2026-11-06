/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/process.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <config.h>
#include <string.h>

int process_kernel_stack_alloc(process_t *p)
{
	void *base;

	if (!p)
		return -EINVAL;
	if (p->kstack_base)
		return 0;

	base = kmalloc_aligned_try(IR0_PROC_KSTACK_SIZE, 16);
	if (!base)
		return -ENOMEM;
	memset(base, 0, IR0_PROC_KSTACK_SIZE);
	p->kstack_base = base;
	/*
	 * Exclusive high water mark (same convention as x86 task_stack.c):
	 * valid bytes are [base, kstack_top). set_current_kernel_stack programs
	 * SP_EL1 so vectors.S can sub #256 for the EL0 sync frame (see
	 * Documentation/ARM64_STACK_MODEL.md).
	 */
	p->kstack_top =
		(uint64_t)(uintptr_t)base + (uint64_t)IR0_PROC_KSTACK_SIZE;
	p->saved_user_rsp = 0;
	return 0;
}

void process_kernel_stack_free(process_t *p)
{
	if (!p || !p->kstack_base)
		return;
	kfree_aligned(p->kstack_base);
	p->kstack_base = NULL;
	p->kstack_top = 0;
	p->saved_user_rsp = 0;
}
