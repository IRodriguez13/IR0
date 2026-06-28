/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_mmap_null_placement.c
 * Description: Host tests — Linux-like top-down mmap(NULL) + stack gap
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <ir0/abi/mmap_contract.h>
#include <stdint.h>

void test_mmap_null_placement(void)
{
	ir0_mmap_occ_t occ[5];
	uintptr_t mmap_base;
	uintptr_t a;
	uintptr_t b;
	uintptr_t fixed;
	uintptr_t search_low;
	uintptr_t stack_start;
	uintptr_t search_end;

	TEST_BEGIN("mmap_null_placement");

	search_low = ir0_mmap_heap_low_limit(0x455000UL, IR0_USER_MMAP_START);
	stack_start = IR0_USER_STACK_TOP - IR0_USER_STACK_SIZE;
	search_end = ir0_mmap_stack_search_end(stack_start);

	ASSERT(search_end == IR0_USER_MMAP_END);
	ASSERT(IR0_USER_MMAP_END + IR0_USER_STACK_MMAP_GAP ==
	       IR0_USER_STACK_GUARD);

	occ[0].start = 0x453000UL;
	occ[0].len = 0x2000UL;
	occ[1].start = stack_start;
	occ[1].len = IR0_USER_STACK_SIZE;
	occ[2].start = IR0_USER_MMAP_START;
	occ[2].len = 0x10000UL;

	mmap_base = 0;
	a = ir0_mmap_pick_topdown(&mmap_base, search_end, search_low,
				  0x1000UL, occ, 3);
	ASSERT(a != 0);
	ASSERT(a >= IR0_MMAP_NULL_MIN_VA);
	ASSERT(a != IR0_USER_MMAP_START);
	ASSERT(!ir0_mmap_occupy(occ, 3, a, 0x1000UL));
	ASSERT(ir0_mmap_respects_stack_gap(a, 0x1000UL, stack_start));

	b = ir0_mmap_pick_topdown(&mmap_base, search_end, search_low,
				  0x1000UL, occ, 3);
	ASSERT(b != 0);
	ASSERT(b != a);
	ASSERT(b < a);
	ASSERT(ir0_mmap_respects_stack_gap(b, 0x1000UL, stack_start));

	occ[3].start = a;
	occ[3].len = 0x1000UL;
	fixed = ir0_mmap_pick_topdown(&mmap_base, search_end, search_low,
				      0x1000UL, occ, 4);
	ASSERT(fixed != 0);
	ASSERT(fixed != a);
	ASSERT(fixed != b);
	ASSERT(ir0_mmap_fixed_requires_exact_va(IR0_MAP_FIXED));

	TEST_END();
}
