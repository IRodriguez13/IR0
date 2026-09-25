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

static size_t append_be32(uint8_t *p, size_t off, uint32_t value)
{
	put_be32(p + off, value);
	return off + 4U;
}

static size_t append_name(uint8_t *p, size_t off, const char *name)
{
	size_t len = strlen(name) + 1U;

	memcpy(p + off, name, len);
	off += len;
	while ((off & 3U) != 0)
		p[off++] = 0;
	return off;
}

static size_t append_prop_u32(uint8_t *p, size_t off, uint32_t nameoff,
			      uint32_t value)
{
	off = append_be32(p, off, 3U);
	off = append_be32(p, off, 4U);
	off = append_be32(p, off, nameoff);
	return append_be32(p, off, value);
}

static size_t build_memory_fdt(uint8_t *fdt, size_t capacity)
{
	static const char strings[] = "#address-cells\0#size-cells\0reg\0";
	const size_t struct_off = 56U;
	size_t off = struct_off;
	size_t strings_off;

	memset(fdt, 0, capacity);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "");
	off = append_prop_u32(fdt, off, 0U, 2U);
	off = append_prop_u32(fdt, off, 15U, 2U);
	off = append_be32(fdt, off, 1U);
	off = append_name(fdt, off, "memory@40000000");
	off = append_be32(fdt, off, 3U);
	off = append_be32(fdt, off, 16U);
	off = append_be32(fdt, off, 27U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x40000000U);
	off = append_be32(fdt, off, 0U);
	off = append_be32(fdt, off, 0x08000000U);
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 2U);
	off = append_be32(fdt, off, 9U);
	strings_off = off;
	memcpy(fdt + strings_off, strings, sizeof(strings));
	off += sizeof(strings);

	put_be32(fdt, 0xd00dfeedU);
	put_be32(fdt + 4, (uint32_t)off);
	put_be32(fdt + 8, (uint32_t)struct_off);
	put_be32(fdt + 12, (uint32_t)strings_off);
	put_be32(fdt + 16, 40U);
	put_be32(fdt + 32, (uint32_t)sizeof(strings));
	put_be32(fdt + 36, (uint32_t)(strings_off - struct_off));
	return off;
}

void test_arm64_boot_info_fdt_contract(void)
{
	uint8_t fdt[64] __attribute__((aligned(8)));
	uint8_t memory_fdt[256] __attribute__((aligned(8)));
	const struct arm64_board_boot_info *info;
	size_t memory_fdt_size;

	memset(fdt, 0, sizeof(fdt));
	put_be32(fdt, 0xd00dfeedU);
	put_be32(fdt + 4, sizeof(fdt));

	TEST_BEGIN("arm64 firmware x0 FDT contract");
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)fdt) == 0);
	info = arm64_board_boot_info();
	ASSERT(info->fdt_valid == 1);
	ASSERT(info->fdt_pa == (uintptr_t)fdt);
	ASSERT(info->fdt_size == sizeof(fdt));
	memory_fdt_size = build_memory_fdt(memory_fdt, sizeof(memory_fdt));
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)memory_fdt) == 0);
	info = arm64_board_boot_info();
	ASSERT(info->fdt_size == memory_fdt_size);
	ASSERT(info->memory_range_count == 1U);
	ASSERT(info->memory[0].base == 0x40000000ULL);
	ASSERT(info->memory[0].size == 0x08000000ULL);
	ASSERT(arm64_fdt_boot_info_init((uintptr_t)(fdt + 1)) != 0);
	ASSERT(arm64_board_boot_info()->fdt_valid == 0);
	TEST_END();
}
