/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <ir0/tlb_sync.h>

static uintptr_t active_root;
static unsigned int page_invalidations;
static unsigned int full_invalidations;

uintptr_t mm_current_root(void)
{
	return active_root;
}

void tlb_invalidate_page(uintptr_t va)
{
	(void)va;
	page_invalidations++;
}

void tlb_invalidate_all(void)
{
	full_invalidations++;
}

void test_tlb_sync_up_contract(void)
{
	address_space_root_t local = (address_space_root_t)(uintptr_t)0x1000;
	address_space_root_t foreign = (address_space_root_t)(uintptr_t)0x2000;

	TEST_BEGIN("address-space-aware UP TLB synchronization");
	active_root = (uintptr_t)local;
	page_invalidations = 0;
	full_invalidations = 0;

	mm_tlb_sync_page(local, 0x4000);
	ASSERT_EQ(page_invalidations, 1);
	mm_tlb_sync_page(foreign, 0x5000);
	ASSERT_EQ(page_invalidations, 1);
	mm_tlb_sync_page(NULL, 0x6000);
	ASSERT_EQ(page_invalidations, 2);

	mm_tlb_sync_all(foreign);
	ASSERT_EQ(full_invalidations, 0);
	mm_tlb_sync_all(local);
	ASSERT_EQ(full_invalidations, 1);
	mm_tlb_sync_all(NULL);
	ASSERT_EQ(full_invalidations, 2);
	TEST_END();
}
