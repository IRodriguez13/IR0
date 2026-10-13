/* SPDX-License-Identifier: GPL-3.0-only */
/* The common boot-memory handoff must reject invented or overlapping RAM. */

#include "test_harness.h"

#include <ir0/boot_memory.h>
#include <ir0/errno.h>

void test_boot_memory_normalization(void)
{
	const struct ir0_phys_range unsorted[] = {
		{ .base = 0x00003001U, .size = 0x00001FFFU },
		{ .base = 0x00001000U, .size = 0x00001000U },
	};
	const struct ir0_phys_range overlapping[] = {
		{ .base = 0x00001000U, .size = 0x00002000U },
		{ .base = 0x00002000U, .size = 0x00001000U },
	};
	struct ir0_phys_range output[2];
	size_t count;

	TEST_BEGIN("boot_memory_normalization");
	ASSERT_EQ(boot_memory_normalize_ranges(unsorted, 2, output, 2, &count), 0);
	ASSERT_EQ(count, 2);
	ASSERT_EQ(output[0].base, 0x00001000U);
	ASSERT_EQ(output[0].size, 0x00001000U);
	ASSERT_EQ(output[1].base, 0x00004000U);
	ASSERT_EQ(output[1].size, 0x00001000U);
	ASSERT_EQ(boot_memory_normalize_ranges(overlapping, 2, output, 2, &count),
		  -EINVAL);
}
