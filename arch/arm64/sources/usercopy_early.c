/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: usercopy_early.c
 * Description: Staged EL0 usercopy for ARM64 early bring-up.
 */

#include "usercopy_early.h"
#include "mmu_early.h"

#include <ir0/tlb.h>

int arm64_early_copy_from_user(void *destination, uint64_t source,
			       uint64_t size)
{
	uint8_t *dst = destination;
	const volatile uint8_t *src;
	uint64_t index;

	if (!destination || size == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok_in_root((uint64_t)mm_current_root(),
					   source, size))
		return -1;
	src = (const volatile uint8_t *)(uintptr_t)source;
	for (index = 0; index < size; index++)
		dst[index] = src[index];
	return 0;
}

int arm64_early_copy_to_user(uint64_t destination, const void *source,
			     uint64_t size)
{
	volatile uint8_t *dst;
	const uint8_t *src = source;
	uint64_t index;

	if (!source || size == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok_in_root((uint64_t)mm_current_root(),
					   destination, size))
		return -1;
	dst = (volatile uint8_t *)(uintptr_t)destination;
	for (index = 0; index < size; index++)
		dst[index] = src[index];
	return 0;
}

void arm64_early_zero_user_page(uint64_t page)
{
	volatile uint8_t *bytes;
	uint64_t index;

	if (!arm64_mmu_user_buf_ok_in_root((uint64_t)mm_current_root(), page,
					   4096UL))
		return;
	bytes = (volatile uint8_t *)(uintptr_t)page;
	for (index = 0; index < 4096UL; index++)
		bytes[index] = 0;
}
