/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"

#include <ir0/arm64_board.h>

#include <stdint.h>
#include <string.h>

static void put_be32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

void test_arm64_boot_info_fdt_contract(void)
{
	uint8_t fdt[64] __attribute__((aligned(8)));
	const struct arm64_board_boot_info *info;

	memset(fdt, 0, sizeof(fdt));
	put_be32(fdt, 0xd00dfeedU);
	put_be32(fdt + 4, sizeof(fdt));

	TEST_BEGIN("arm64 firmware x0 FDT contract");
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)fdt) == 0);
	info = arm64_board_boot_info();
	ASSERT(info->fdt_valid == 1);
	ASSERT(info->fdt_pa == (uintptr_t)fdt);
	ASSERT(info->fdt_size == sizeof(fdt));
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)(fdt + 1)) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	TEST_END();
}
