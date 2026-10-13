/* SPDX-License-Identifier: GPL-3.0-only */
/* PMM must keep dense metadata without inventing physical frames in gaps. */

#include "test_harness.h"

#include <mm/pmm.h>

void test_pmm_banked_physical_layout(void)
{
	const struct ir0_phys_range ranges[] = {
		{ .base = 0x00030000U, .size = 2U * PMM_FRAME_SIZE },
		{ .base = 0x00010000U, .size = 1U * PMM_FRAME_SIZE },
	};
	uintptr_t first;
	uintptr_t second;
	uintptr_t third;

	TEST_BEGIN("pmm_banked_physical_layout");
	ASSERT_EQ(pmm_init_ranges(ranges, 2), 0);
	ASSERT_EQ(pmm_total_frames(), 3);
	ASSERT_EQ(pmm_get_start(), 0x00010000U);
	ASSERT_EQ(pmm_get_end(), 0x00032000U);

	first = pmm_alloc_frame();
	second = pmm_alloc_frame();
	third = pmm_alloc_frame();
	ASSERT_EQ(first, 0x00010000U);
	ASSERT_EQ(second, 0x00030000U);
	ASSERT_EQ(third, 0x00031000U);
	ASSERT_EQ(pmm_alloc_frame(), 0);
	ASSERT_EQ(pmm_frame_phys(0), first);
	ASSERT_EQ(pmm_frame_phys(1), second);
	ASSERT_EQ(pmm_frame_phys(2), third);

	pmm_free_frame(second);
	ASSERT_EQ(pmm_alloc_frame(), second);
}
