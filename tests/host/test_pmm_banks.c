/* SPDX-License-Identifier: GPL-3.0-only */
/* PMM must keep dense metadata without inventing physical frames in gaps. */

#include "test_harness.h"

#include <ir0/boot_memory.h>
#include <ir0/errno.h>
#include <ir0/mm_alloc.h>
#include <ir0/reclaim.h>
#include <mm/pmm.h>

void host_kmem_fail_after(long successful_allocations);

static uintptr_t policy_reclaim_frame;

static uint64_t policy_reclaim_count(void *context)
{
	(void)context;
	return policy_reclaim_frame != 0 ? 1 : 0;
}

static int policy_reclaim_scan(void *context, uint64_t target,
			       uint64_t *reclaimed)
{
	(void)context;
	if (target == 0 || policy_reclaim_frame == 0)
	{
		*reclaimed = 0;
		return 0;
	}
	pmm_free_frame(policy_reclaim_frame);
	policy_reclaim_frame = 0;
	*reclaimed = 1;
	return 0;
}

static const struct ir0_reclaimer policy_reclaimer = {
	.name = "host-policy-frame",
	.count = policy_reclaim_count,
	.scan = policy_reclaim_scan,
	.ctx = NULL,
};

void test_pmm_banked_physical_layout(void)
{
	const struct ir0_phys_range usable[] = {
		{ .base = 0x00010000U, .size = 3U * PMM_FRAME_SIZE },
	};
	const struct ir0_phys_range reserved[] = {
		{ .base = 0x00011000U, .size = PMM_FRAME_SIZE },
	};
	struct ir0_phys_range ranges[2];
	size_t range_count;
	uintptr_t first;
	uintptr_t second;

	TEST_BEGIN("pmm_banked_physical_layout");
	ASSERT_EQ(boot_memory_exclude_reserved(usable, 1, reserved, 1, ranges, 2,
					       &range_count), 0);
	ASSERT_EQ(range_count, 2);
	host_kmem_fail_after(1);
	ASSERT_EQ(pmm_init_ranges(ranges, range_count), -1);
	ASSERT_EQ(pmm_total_frames(), 0);
	host_kmem_fail_after(-1);
	ASSERT_EQ(pmm_init_ranges(ranges, range_count), 0);
	ASSERT_EQ(pmm_total_frames(), 2);
	ASSERT_EQ(pmm_get_start(), 0x00010000U);
	ASSERT_EQ(pmm_get_end(), 0x00013000U);

	first = pmm_alloc_frame();
	second = pmm_alloc_frame();
	ASSERT_EQ(first, 0x00010000U);
	ASSERT_EQ(second, 0x00012000U);
	ASSERT_EQ(pmm_alloc_frame(), 0);
	ASSERT_EQ(pmm_frame_phys(0), first);
	ASSERT_EQ(pmm_frame_phys(1), second);

	pmm_free_frame(second);
	ASSERT_EQ(pmm_alloc_frame(), second);
	policy_reclaim_frame = second;
	ASSERT_EQ(mm_reclaimer_register(&policy_reclaimer), 0);
	ASSERT_EQ(mm_alloc_frame(MM_ALLOC_ATOMIC), 0);
	ASSERT_EQ(policy_reclaim_frame, second);
	ASSERT_EQ(mm_alloc_frame(MM_ALLOC_DIRECT_RECLAIM), second);
	ASSERT_EQ(policy_reclaim_frame, 0);
	TEST_END();
}
