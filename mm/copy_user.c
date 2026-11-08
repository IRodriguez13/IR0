// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025–2026  Iván Rodriguez
 *
 * File: copy_user.c
 * Description: Portable copy_to/from_user with mode-aware validation.
 *              USER_MODE walks process page tables (COW-safe). KERNEL_MODE
 *              bypass is dbgshell/ktest only — never use for production
 *              userspace pointers.
 */

#include <ir0/copy_user.h>
#include <ir0/mm.h>
#include "process.h"
#include <mm/paging.h>
#include <string.h>
#include <config.h>
#include <ir0/logging.h>

int is_user_address_checked(const void *addr, size_t size, int check_mapped)
{
	uintptr_t start = (uintptr_t)addr;
	uintptr_t end = start + size;

	if (addr == NULL)
		return 0;

	if (end < start)
		return 0;

	if (!mm_user_va_ok(start, size))
		return 0;

	if (check_mapped)
	{
		process_t *current = process_get_current();

		if (!current || !process_pgd(current))
			return 0;

		for (uintptr_t addr_check = start & ~0xFFFUL; addr_check < end;
		     addr_check += 0x1000UL)
		{
			int mapped = is_page_mapped_in_directory(
				process_pgd(current), addr_check, NULL);

			if (mapped != 1)
			{
#if DEBUG_SYSCALLS
				log_info("COPY_USER",
					 "Address range contains unmapped pages");
#endif
				return 0;
			}
		}
	}

	return 1;
}

int is_user_address(const void *addr, size_t size)
{
	/*
	 * Fast range check only; unmapped pages fault on copy. Callers that must
	 * reject unmapped user pointers before touching page tables (e.g. exec
	 * argv/envp walks) use is_user_address_checked(addr, size, 1).
	 */
	return is_user_address_checked(addr, size, 0);
}

int copy_to_user(void *dst, const void *src, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	/* KERNEL_MODE bypass (dbgshell, embedded init) */
	if (current && current->mode == KERNEL_MODE)
	{
		memcpy(dst, src, n);
		return 0;
	}

	if (!is_user_address(dst, n))
		return -EFAULT;

	/*
	 * Walk target process page tables via MM facade — safe under any active
	 * active address-space root and fails cleanly on guard/unmapped pages (no
	 * kernel page-fault panic).
	 */
	if (current && process_pgd(current))
	{
		if (copy_to_user_mm(process_pgd(current), (uintptr_t)dst, src,
				    n) != 0)
			return -EFAULT;
		return 0;
	}

	return -EFAULT;
}

int copy_from_user(void *dst, const void *src, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	if (current && current->mode == KERNEL_MODE)
	{
		memcpy(dst, src, n);
		return 0;
	}

	if (!is_user_address(src, n))
		return -EFAULT;

	if (current && process_pgd(current))
	{
		if (copy_from_user_mm(process_pgd(current), (uintptr_t)src, dst,
				      n) != 0)
			return -EFAULT;
		return 0;
	}

	return -EFAULT;
}

int clear_user(void *dst, size_t n)
{
	process_t *current = process_get_current();

	if (n == 0)
		return 0;

	if (current && current->mode == KERNEL_MODE)
	{
		memset(dst, 0, n);
		return 0;
	}

	if (!is_user_address(dst, n))
		return -EFAULT;

	if (!current || !process_pgd(current))
		return -EFAULT;

	if (clear_user_mm(process_pgd(current), (uintptr_t)dst, n) != 0)
		return -EFAULT;
	return 0;
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
