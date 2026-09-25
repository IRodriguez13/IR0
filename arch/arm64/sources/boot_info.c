/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_info.c
 * Description: ARM64 firmware boot information and FDT header validation.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arm64_board.h>

#include <stdint.h>
#include <stddef.h>

#define FDT_MAGIC        0xd00dfeedU
#define FDT_HEADER_SIZE  40U
#define FDT_MAX_SIZE     (2U * 1024U * 1024U)
#define FDT_BEGIN_NODE   1U
#define FDT_END_NODE     2U
#define FDT_PROP         3U
#define FDT_NOP          4U
#define FDT_END          9U

/* Written by the stackless entry assembly before x0 is reused. */
uintptr_t arm64_firmware_fdt;

static struct arm64_board_boot_info g_boot_info;

static uint32_t read_be32(const volatile uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t read_cells(const volatile uint8_t *p, uint32_t cells)
{
	uint64_t value = 0;
	uint32_t i;

	for (i = 0; i < cells; i++)
		value = (value << 32) | read_be32(p + i * 4U);
	return value;
}

static int node_is_memory(const volatile uint8_t *name, uint32_t available)
{
	static const char prefix[] = "memory@";
	uint32_t i;

	if (available < sizeof(prefix) - 1U)
		return 0;
	for (i = 0; i < sizeof(prefix) - 1U; i++)
	{
		if (name[i] != (uint8_t)prefix[i])
			return 0;
	}
	return 1;
}

static int prop_name_is(const volatile uint8_t *strings, uint32_t strings_size,
			uint32_t nameoff, const char *wanted)
{
	uint32_t i = 0;

	if (nameoff >= strings_size)
		return 0;
	while (wanted[i] && nameoff + i < strings_size)
	{
		if (strings[nameoff + i] != (uint8_t)wanted[i])
			return 0;
		i++;
	}
	return wanted[i] == '\0' && nameoff + i < strings_size &&
	       strings[nameoff + i] == '\0';
}

static void parse_memory_ranges(const volatile uint8_t *fdt, uint32_t total)
{
	const volatile uint8_t *structure;
	const volatile uint8_t *strings;
	uint32_t struct_off = read_be32(fdt + 8);
	uint32_t strings_off = read_be32(fdt + 12);
	uint32_t strings_size = read_be32(fdt + 32);
	uint32_t struct_size = read_be32(fdt + 36);
	uint32_t address_cells = 2;
	uint32_t size_cells = 1;
	uint32_t off = 0;
	uint32_t depth = 0;
	uint32_t memory_depth = 0;

	if (struct_off > total || struct_size > total - struct_off ||
	    strings_off > total || strings_size > total - strings_off)
		return;
	structure = fdt + struct_off;
	strings = fdt + strings_off;

	while (off + 4U <= struct_size)
	{
		uint32_t token = read_be32(structure + off);

		off += 4U;
		if (token == FDT_BEGIN_NODE)
		{
			uint32_t name_start = off;

			while (off < struct_size && structure[off] != 0)
				off++;
			if (off >= struct_size)
				return;
			depth++;
			if (depth == 2U &&
			    node_is_memory(structure + name_start, off - name_start))
				memory_depth = depth;
			off = (off + 4U) & ~3U;
		}
		else if (token == FDT_END_NODE)
		{
			if (memory_depth == depth)
				memory_depth = 0;
			if (depth == 0)
				return;
			depth--;
		}
		else if (token == FDT_PROP)
		{
			uint32_t len;
			uint32_t nameoff;
			const volatile uint8_t *value;
			uint32_t tuple_cells;

			if (off + 8U > struct_size)
				return;
			len = read_be32(structure + off);
			nameoff = read_be32(structure + off + 4U);
			off += 8U;
			if (len > struct_size - off)
				return;
			value = structure + off;
			if (depth == 1U && len >= 4U &&
			    prop_name_is(strings, strings_size, nameoff, "#address-cells"))
				address_cells = read_be32(value);
			else if (depth == 1U && len >= 4U &&
				 prop_name_is(strings, strings_size, nameoff, "#size-cells"))
				size_cells = read_be32(value);
			else if (memory_depth == depth &&
				 prop_name_is(strings, strings_size, nameoff, "reg") &&
				 address_cells > 0U && address_cells <= 2U &&
				 size_cells > 0U && size_cells <= 2U)
			{
				tuple_cells = address_cells + size_cells;
				while (len >= tuple_cells * 4U &&
				       g_boot_info.memory_range_count < ARM64_BOOT_MEMORY_RANGES_MAX)
				{
					struct ir0_phys_range *range =
						&g_boot_info.memory[g_boot_info.memory_range_count];

					range->base = read_cells(value, address_cells);
					range->size = read_cells(value + address_cells * 4U,
							       size_cells);
					if (range->size != 0)
						g_boot_info.memory_range_count++;
					value += tuple_cells * 4U;
					len -= tuple_cells * 4U;
				}
			}
			off = (off + read_be32(structure + off - 8U) + 3U) & ~3U;
		}
		else if (token == FDT_NOP)
		{
			continue;
		}
		else if (token == FDT_END)
		{
			return;
		}
		else
		{
			return;
		}
	}
}

int arm64_fdt_boot_info_init(uintptr_t fdt_pa)
{
	const volatile uint8_t *header;
	uint32_t size;

	g_boot_info.fdt_pa = fdt_pa;
	g_boot_info.fdt_size = 0;
	g_boot_info.fdt_magic = 0;
	g_boot_info.fdt_valid = 0;
	g_boot_info.memory_range_count = 0;

	/* Linux arm64 boot protocol requires an 8-byte-aligned DTB in RAM. */
	if (!fdt_pa || (fdt_pa & 7U) != 0)
	{
		return -1;
	}

	header = (const volatile uint8_t *)fdt_pa;
	g_boot_info.fdt_magic = read_be32(header);
	if (g_boot_info.fdt_magic != FDT_MAGIC)
	{
		return -1;
	}
	size = read_be32(header + 4);
	g_boot_info.fdt_size = size;
	if (size < FDT_HEADER_SIZE || size > FDT_MAX_SIZE)
	{
		return -1;
	}

	g_boot_info.fdt_valid = 1;
	parse_memory_ranges(header, size);
	return 0;
}

const struct arm64_board_boot_info *arm64_board_boot_info(void)
{
	return &g_boot_info;
}
