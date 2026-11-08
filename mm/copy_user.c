// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025–2026  Iván Rodriguez
 *
 * File: copy_user.c
 * Description: access_ok() and copy_{to,from}_user() for the current process.
 *              User processes are checked against the ISA user VA window and
 *              copied through their page tables (COW-safe, -EFAULT instead
 *              of a kernel fault). Kernel threads pass kernel buffers.
 */

#include <ir0/copy_user.h>
#include <ir0/mm.h>
#include "process.h"
#include <mm/paging.h>
#include <string.h>

/*
 * Kernel threads (KERNEL_MODE: dbgshell, ktest harness) call syscalls with
 * kernel buffers, so every user-pointer check passes their pointers through,
 * like Linux uaccess_kernel(). The mode is fixed at spawn, inherited by
 * fork, and exec requires USER_MODE, so no pointer from userspace is ever
 * checked under this rule.
 */
static int uaccess_kernel(const process_t *current)
{
	return current && current->mode == KERNEL_MODE;
}

int access_ok(const void *addr, size_t size)
{
	if (uaccess_kernel(process_get_current()))
		return 1;
	return mm_user_va_ok((uintptr_t)addr, size);
}

int access_ok_mapped(const void *addr, size_t size)
{
	process_t *current = process_get_current();
	uintptr_t start = (uintptr_t)addr;
	uintptr_t page;

	if (uaccess_kernel(current))
		return 1;
	if (!mm_user_va_ok(start, size))
		return 0;
	if (!current || !process_pgd(current))
		return 0;

	for (page = start & ~(uintptr_t)(PAGE_SIZE_4KB - 1); page < start + size;
	     page += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(process_pgd(current), page,
						NULL) != 1)
			return 0;
	}

	return 1;
}

int copy_to_user(void *dst, const void *src, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	if (uaccess_kernel(current))
	{
		memcpy(dst, src, n);
		return 0;
	}

	/*
	 * Walk the process page tables instead of dereferencing @dst: safe under
	 * any active root, and guard/unmapped/supervisor pages give -EFAULT
	 * instead of a kernel page fault.
	 */
	if (!current || !process_pgd(current))
		return -EFAULT;
	return copy_to_user_mm(process_pgd(current), (uintptr_t)dst, src, n);
}

int copy_from_user(void *dst, const void *src, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	if (uaccess_kernel(current))
	{
		memcpy(dst, src, n);
		return 0;
	}

	if (!current || !process_pgd(current))
		return -EFAULT;
	return copy_from_user_mm(process_pgd(current), (uintptr_t)src, dst, n);
}

int clear_user(void *dst, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	if (uaccess_kernel(current))
	{
		memset(dst, 0, n);
		return 0;
	}

	if (!current || !process_pgd(current))
		return -EFAULT;
	return clear_user_mm(process_pgd(current), (uintptr_t)dst, n);
}

long strncpy_from_user(char *dst, const char *src, long count)
{
	uintptr_t addr = (uintptr_t)src;
	long done = 0;

	if (count <= 0)
		return 0;
	if (!dst || !src)
		return -EFAULT;

	/*
	 * Copy at most to the end of each source page, so a string that ends
	 * just before an unmapped page never touches it.
	 */
	while (done < count)
	{
		size_t chunk = PAGE_SIZE_4KB - ((addr + (uintptr_t)done) &
						(PAGE_SIZE_4KB - 1));
		size_t i;

		if (chunk > (size_t)(count - done))
			chunk = (size_t)(count - done);
		if (copy_from_user(dst + done, (const void *)(addr + (uintptr_t)done),
				   chunk) != 0)
			return -EFAULT;
		for (i = 0; i < chunk; i++)
		{
			if (dst[done + (long)i] == '\0')
				return done + (long)i;
		}
		done += (long)chunk;
	}

	return count;
}
