/* SPDX-License-Identifier: GPL-3.0-only */
/* The common boot-memory handoff must reject invented or overlapping RAM. */

#include "test_harness.h"

#include <ir0/boot_memory.h>
#include <ir0/boot_params.h>
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
	const struct ir0_phys_range usable[] = {
		{ .base = 0x00010000U, .size = 0x00008000U },
	};
	const struct ir0_phys_range reserved[] = {
		{ .base = 0x00012FF0U, .size = 0x00002020U },
	};
	const struct ir0_boot_params params = {
		.protocol = IR0_BOOT_PROTOCOL_UNKNOWN,
		.info = 1,
	};
	struct ir0_phys_range output[4];
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
	ASSERT_EQ(boot_memory_exclude_reserved(usable, 1, reserved, 1, output, 4,
					       &count), 0);
	ASSERT_EQ(count, 2);
	ASSERT_EQ(output[0].base, 0x00010000U);
	ASSERT_EQ(output[0].size, 0x00002000U);
	ASSERT_EQ(output[1].base, 0x00016000U);
	ASSERT_EQ(output[1].size, 0x00002000U);
	ASSERT_EQ(boot_memory_snapshot_init(&params), 0);
	ASSERT_STR_EQ(boot_memory_cmdline(), "ir0");
	ASSERT(boot_memory_range_is_reserved(0x12000U, 0x1000U));
	ASSERT(!boot_memory_range_is_reserved(0x10000U, 0x1000U));
	ASSERT_EQ(boot_memory_get_allocatable_ranges(output, 4, &count), 0);
	ASSERT_EQ(count, 2);
	ASSERT_EQ(output[0].base, 0x00010000U);
	ASSERT_EQ(output[0].size, 0x00002000U);
	ASSERT_EQ(output[1].base, 0x00014000U);
	ASSERT_EQ(output[1].size, 0x00004000U);
	TEST_END();
}
