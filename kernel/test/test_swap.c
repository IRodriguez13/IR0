/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_swap.c
 * Description: ktest — SWAPSPACE2 header, area install/remove, slot refs.
 */

#include "test/ktest_harness.h"

#include <ir0/arch_mm.h>
#include <ir0/errno.h>
#include <ir0/swap.h>
#include <string.h>

void ktest_swap_header_and_area(void)
{
	unsigned char page[IR0_SWAP_PAGE_SIZE];
	struct ir0_swap_header_info hdr;
	struct ir0_swap_extent ext;
	unsigned type;
	uint64_t offset;

	KTEST_BEGIN("swap_header_and_area");

	memset(page, 0, sizeof(page));
	KASSERT(ir0_swap_parse_header(page, sizeof(page), &hdr) == -EINVAL);

	page[1024] = 1;
	page[1028] = 8;
	memcpy(page + IR0_SWAP_PAGE_SIZE - 10, IR0_SWAP_MAGIC, 10);
	KASSERT(ir0_swap_parse_header(page, sizeof(page), &hdr) == 0);
	KASSERT_EQ(hdr.last_page, 8U);

	ext.file_page = 0;
	ext.phys_lba = 16;
	ext.nr_pages = 9;
	KASSERT(ir0_swap_area_install("/ktest-swap", IR0_SWAP_KIND_BLOCK, 0, 2,
				      &ext, 1, 8) == 0);
	KASSERT(ir0_swap_path_busy("/ktest-swap") == 1);
	KASSERT(ir0_swap_slot_alloc(&type, &offset) == 0);
	KASSERT(ir0_swap_slot_get(type, offset) == 0);
	KASSERT(ir0_swap_slot_put(type, offset) == 0);
	KASSERT(ir0_swap_slot_put(type, offset) == 0);
	KASSERT(ir0_swap_area_remove("/ktest-swap") == 0);
	KASSERT(ir0_swap_path_busy("/ktest-swap") == 0);

	{
		uint64_t pte = mm_make_swap_pte(2, 7);

		KASSERT(!mm_pte_present(pte));
		KASSERT(mm_pte_is_swap(pte));
		mm_pte_swap_decode(pte, &type, &offset);
		KASSERT_EQ(2U, type);
		KASSERT_EQ(7ULL, offset);
	}

	KTEST_END();
}
