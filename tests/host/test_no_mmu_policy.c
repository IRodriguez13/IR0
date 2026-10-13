/* SPDX-License-Identifier: GPL-3.0-only */
/* Contract for the future constrained-ISA direct-address-space policy. */

#include "test_harness.h"

#include <ir0/errno.h>
#include <ir0/no_mmu.h>

static int test_physical_range_ok(uintptr_t base, size_t size)
{
	return base >= 0x1000U && size <= 0x1000U && base + size <= 0x3000U;
}

void test_no_mmu_policy_contract(void)
{
	const struct ir0_no_mmu_policy policy = {
		.user_start = 0x1000U,
		.user_end = 0x3000U,
		.direct_start = 0x0000U,
		.direct_end = 0x3000U,
		.physical_range_ok = test_physical_range_ok,
	};

	TEST_BEGIN("no_mmu_policy_contract");
	ASSERT_EQ(no_mmu_user_range_ok(0x1000U, 1U), -ENODEV);
	ASSERT_EQ(no_mmu_configure(&policy), 0);
	ASSERT_EQ(no_mmu_user_range_ok(0x1000U, 0x1000U), 0);
	ASSERT_EQ(no_mmu_user_range_ok(0x2800U, 0x1000U), -EFAULT);
	ASSERT_EQ(no_mmu_physical_range_ok(0x0000U, 0x1000U), -EFAULT);
	ASSERT_EQ(no_mmu_physical_range_ok(0x1000U, 0x1000U), 0);
	ASSERT_EQ(no_mmu_physical_range_ok(0x2000U, 0x1001U), -EFAULT);
	ASSERT_EQ(no_mmu_virtual_remap_status(), -ENOSYS);
}
