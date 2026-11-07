/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fdt_reader.c
 * Description: Bounded ARM64-private flattened device tree readers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "boot_info_private.h"

#include <stddef.h>
#include <stdint.h>

uint32_t arm64_fdt_read_be32(const volatile uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint64_t arm64_fdt_read_cells(const volatile uint8_t *p, uint32_t cells)
{
	uint64_t value = 0;
	uint32_t i;

	for (i = 0; i < cells; i++)
	{
		value = (value << 32) | arm64_fdt_read_be32(p + i * 4U);
	}
	return value;
}

int arm64_fdt_view_init(struct arm64_fdt_view *view,
			const volatile uint8_t *fdt, uint32_t total)
{
	uint32_t structure_offset;
	uint32_t strings_offset;
	uint32_t structure_size;
	uint32_t strings_size;

	if (view == NULL || fdt == NULL || total < ARM64_FDT_HEADER_SIZE)
	{
		return -1;
	}
	structure_offset = arm64_fdt_read_be32(fdt + 8U);
	strings_offset = arm64_fdt_read_be32(fdt + 12U);
	strings_size = arm64_fdt_read_be32(fdt + 32U);
	structure_size = arm64_fdt_read_be32(fdt + 36U);
	if (structure_offset < ARM64_FDT_HEADER_SIZE ||
	    strings_offset < ARM64_FDT_HEADER_SIZE ||
	    structure_offset > total ||
	    structure_size > total - structure_offset ||
	    strings_offset > total || strings_size > total - strings_offset)
	{
		return -1;
	}
	view->blob = fdt;
	view->total_size = total;
	view->structure = fdt + structure_offset;
	view->structure_size = structure_size;
	view->strings = fdt + strings_offset;
	view->strings_size = strings_size;
	return 0;
}

int arm64_fdt_name_is(const volatile uint8_t *name, uint32_t available,
		      const char *wanted)
{
	uint32_t i = 0;

	while (wanted[i] != '\0' && i < available)
	{
		if (name[i] != (uint8_t)wanted[i])
		{
			return 0;
		}
		i++;
	}
	return wanted[i] == '\0' && i == available;
}

int arm64_fdt_name_has_prefix(const volatile uint8_t *name,
			      uint32_t available, const char *prefix)
{
	uint32_t i = 0;

	while (prefix[i] != '\0')
	{
		if (i >= available || name[i] != (uint8_t)prefix[i])
		{
			return 0;
		}
		i++;
	}
	return 1;
}

int arm64_fdt_prop_name_is(const struct arm64_fdt_view *view,
			   uint32_t name_offset, const char *wanted)
{
	uint32_t i = 0;

	if (name_offset >= view->strings_size)
	{
		return 0;
	}
	while (wanted[i] != '\0' && name_offset + i < view->strings_size)
	{
		if (view->strings[name_offset + i] != (uint8_t)wanted[i])
		{
			return 0;
		}
		i++;
	}
	return wanted[i] == '\0' && name_offset + i < view->strings_size &&
	       view->strings[name_offset + i] == '\0';
}

int arm64_fdt_string_list_has(const volatile uint8_t *value, uint32_t length,
			      const char *wanted)
{
	uint32_t offset = 0;

	while (offset < length)
	{
		uint32_t item_length = 0;

		while (offset + item_length < length &&
		       value[offset + item_length] != 0U)
		{
			item_length++;
		}
		if (offset + item_length >= length)
		{
			return 0;
		}
		if (arm64_fdt_name_is(value + offset, item_length, wanted))
		{
			return 1;
		}
		offset += item_length + 1U;
	}
	return 0;
}

int arm64_fdt_string_list_has_prefix(const volatile uint8_t *value,
				     uint32_t length, const char *prefix)
{
	uint32_t offset = 0;

	while (offset < length)
	{
		uint32_t item_length = 0;

		while (offset + item_length < length &&
		       value[offset + item_length] != 0U)
		{
			item_length++;
		}
		if (offset + item_length >= length)
		{
			return 0;
		}
		if (arm64_fdt_name_has_prefix(value + offset, item_length, prefix))
		{
			return 1;
		}
		offset += item_length + 1U;
	}
	return 0;
}

int arm64_boot_append_range(struct ir0_phys_range *ranges, uint32_t *count,
			    uint32_t maximum, uint64_t base, uint64_t size)
{
	if (size == 0U || base > UINT64_MAX - size)
	{
		return 0;
	}
	if (*count >= maximum)
	{
		return -1;
	}
	ranges[*count].base = base;
	ranges[*count].size = size;
	(*count)++;
	return 0;
}
