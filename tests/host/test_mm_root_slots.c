/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Root-slot ownership policy: unmap must reclaim a level1 table in every
 * slot the architecture assigns to the process, not just the first 256.
 * ARM64 owns all 512 TTBR0 L0 slots; x86-64 owns the lower half.
 */

#include "test_harness.h"

#include <ir0/mm.h>

/* Fake the ARM64 geometry: the whole root belongs to the process. */
unsigned mm_user_root_slots(void)
{
	return 512;
}

unsigned mm_root_slots(void)
{
	return 512;
}

void test_mm_user_root_slot_contract(void)
{
	TEST_BEGIN("mm_user_root_slot follows the arch facade");

	ASSERT(mm_user_root_slot(0));
	ASSERT(mm_user_root_slot(255));
	/* The old unmap path stopped here and leaked ARM64 level1 tables. */
	ASSERT(mm_user_root_slot(256));
	ASSERT(mm_user_root_slot(511));
	ASSERT(!mm_user_root_slot(512));

	TEST_END();
}
