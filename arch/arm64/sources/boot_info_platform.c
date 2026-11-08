/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_info_platform.c
 * Description: ARM64 FDT memory, chosen, reservation and framebuffer parsing.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "boot_info_private.h"

#include <stdint.h>

/*
 * simple-framebuffer "format" strings (Linux
 * Documentation/devicetree/bindings/display/simple-framebuffer.yaml) and the
 * pixel layout each one names. Alpha and padding bits are not drawn.
 */
struct arm64_simplefb_format
{
	const char *name;
	uint8_t bpp;
	uint8_t red_pos;
	uint8_t red_size;
	uint8_t green_pos;
	uint8_t green_size;
	uint8_t blue_pos;
	uint8_t blue_size;
};

static const struct arm64_simplefb_format g_simplefb_formats[] = {
	{ "r5g6b5", 16U, 11U, 5U, 5U, 6U, 0U, 5U },
	{ "x1r5g5b5", 16U, 10U, 5U, 5U, 5U, 0U, 5U },
	{ "a1r5g5b5", 16U, 10U, 5U, 5U, 5U, 0U, 5U },
	{ "r8g8b8", 24U, 16U, 8U, 8U, 8U, 0U, 8U },
	{ "x8r8g8b8", 32U, 16U, 8U, 8U, 8U, 0U, 8U },
	{ "a8r8g8b8", 32U, 16U, 8U, 8U, 8U, 0U, 8U },
	{ "x8b8g8r8", 32U, 0U, 8U, 8U, 8U, 16U, 8U },
	{ "a8b8g8r8", 32U, 0U, 8U, 8U, 8U, 16U, 8U },
	{ "x2r10g10b10", 32U, 20U, 10U, 10U, 10U, 0U, 10U },
	{ "a2r10g10b10", 32U, 20U, 10U, 10U, 10U, 0U, 10U },
};

/* Decode a simple-framebuffer "format"; unknown formats leave bpp at 0. */
static void framebuffer_set_format(struct ir0_boot_framebuffer *fb,
				   const volatile uint8_t *value,
				   uint32_t length)
{
	uint32_t i;

	for (i = 0U; i < sizeof(g_simplefb_formats) /
			     sizeof(g_simplefb_formats[0]); i++)
	{
		const struct arm64_simplefb_format *format =
		    &g_simplefb_formats[i];

		if (!arm64_fdt_string_list_has(value, length, format->name))
		{
			continue;
		}
		fb->bpp = format->bpp;
		fb->type = IR0_BOOT_FB_TYPE_RGB;
		fb->red_pos = format->red_pos;
		fb->red_size = format->red_size;
		fb->green_pos = format->green_pos;
		fb->green_size = format->green_size;
		fb->blue_pos = format->blue_pos;
		fb->blue_size = format->blue_size;
		return;
	}
}

static int node_is_memory(const volatile uint8_t *name, uint32_t length)
{
	return arm64_fdt_name_is(name, length, "memory") ||
	       arm64_fdt_name_has_prefix(name, length, "memory@");
}

int arm64_boot_parse_reservations(struct arm64_boot_parse_context *context,
				  const volatile uint8_t *fdt,
				  uint32_t total)
{
	uint32_t offset;

	if (context == NULL || context->info == NULL || fdt == NULL ||
	    total < ARM64_FDT_HEADER_SIZE)
	{
		return -1;
	}
	offset = arm64_fdt_read_be32(fdt + 16U);
	if (offset < ARM64_FDT_HEADER_SIZE || offset > total)
	{
		return -1;
	}
	while (offset <= total && total - offset >= 16U)
	{
		uint64_t base = arm64_fdt_read_cells(fdt + offset, 2U);
		uint64_t size = arm64_fdt_read_cells(fdt + offset + 8U, 2U);

		offset += 16U;
		if (base == 0U && size == 0U)
		{
			return 0;
		}
		if (arm64_boot_append_range(
			    context->info->reserved,
			    &context->info->reserved_range_count,
			    ARM64_BOOT_RESERVED_RANGES_MAX, base, size) != 0)
		{
			return -1;
		}
	}
	return -1;
}

int arm64_boot_parse_platform(struct arm64_boot_parse_context *context,
			      const volatile uint8_t *fdt, uint32_t total)
{
	struct arm64_fdt_view view;
	struct arm64_board_boot_info *info;
	uint32_t address_cells = 2U;
	uint32_t size_cells = 1U;
	uint32_t reserved_address_cells = 2U;
	uint32_t reserved_size_cells = 1U;
	uint32_t memory_depth = 0U;
	uint32_t framebuffer_depth = 0U;
	uint32_t chosen_depth = 0U;
	uint32_t reserved_depth = 0U;
	uint32_t reserved_child_depth = 0U;
	uint32_t depth = 0U;
	uint32_t offset = 0U;

	if (context == NULL || context->info == NULL ||
	    arm64_fdt_view_init(&view, fdt, total) != 0)
	{
		return -1;
	}
	info = context->info;
	while (offset <= view.structure_size &&
	       view.structure_size - offset >= 4U)
	{
		uint32_t token = arm64_fdt_read_be32(view.structure + offset);

		offset += 4U;
		if (token == ARM64_FDT_BEGIN_NODE)
		{
			uint32_t name_start = offset;

			while (offset < view.structure_size &&
			       view.structure[offset] != 0U)
			{
				offset++;
			}
			if (offset >= view.structure_size ||
			    depth >= ARM64_FDT_NODE_DEPTH_MAX)
			{
				return -1;
			}
			depth++;
			if (depth == 2U &&
			    node_is_memory(view.structure + name_start,
					   offset - name_start))
			{
				memory_depth = depth;
			}
			if (depth == 2U &&
			    arm64_fdt_name_has_prefix(
				view.structure + name_start, offset - name_start,
				"framebuffer@"))
			{
				framebuffer_depth = depth;
			}
			if (depth == 2U &&
			    arm64_fdt_name_is(view.structure + name_start,
					      offset - name_start, "chosen"))
			{
				chosen_depth = depth;
			}
			if (depth == 2U &&
			    arm64_fdt_name_is(view.structure + name_start,
					      offset - name_start,
					      "reserved-memory"))
			{
				reserved_depth = depth;
			}
			else if (reserved_depth != 0U &&
				 depth == reserved_depth + 1U)
			{
				reserved_child_depth = depth;
			}
			offset = (offset + 4U) & ~3U;
		}
		else if (token == ARM64_FDT_END_NODE)
		{
			if (depth == 0U)
			{
				return -1;
			}
			if (memory_depth == depth)
			{
				memory_depth = 0U;
			}
			if (framebuffer_depth == depth)
			{
				framebuffer_depth = 0U;
			}
			if (reserved_child_depth == depth)
			{
				reserved_child_depth = 0U;
			}
			if (chosen_depth == depth)
			{
				chosen_depth = 0U;
			}
			if (reserved_depth == depth)
			{
				reserved_depth = 0U;
			}
			depth--;
		}
		else if (token == ARM64_FDT_PROP)
		{
			const volatile uint8_t *value;
			uint32_t property_length;
			uint32_t encoded_length;
			uint32_t name_offset;
			uint32_t tuple_cells;

			if (offset > view.structure_size ||
			    view.structure_size - offset < 8U)
			{
				return -1;
			}
			property_length =
			    arm64_fdt_read_be32(view.structure + offset);
			encoded_length = property_length;
			name_offset =
			    arm64_fdt_read_be32(view.structure + offset + 4U);
			offset += 8U;
			if (property_length > view.structure_size - offset)
			{
				return -1;
			}
			value = view.structure + offset;
			if (depth == 1U && property_length == 4U &&
			    arm64_fdt_prop_name_is(&view, name_offset,
						   "#address-cells"))
			{
				address_cells = arm64_fdt_read_be32(value);
			}
			else if (depth == 1U && property_length == 4U &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"#size-cells"))
			{
				size_cells = arm64_fdt_read_be32(value);
			}
			else if (reserved_depth == depth &&
				 property_length == 4U &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"#address-cells"))
			{
				reserved_address_cells = arm64_fdt_read_be32(value);
			}
			else if (reserved_depth == depth &&
				 property_length == 4U &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"#size-cells"))
			{
				reserved_size_cells = arm64_fdt_read_be32(value);
			}
			else if (memory_depth == depth &&
				 arm64_fdt_prop_name_is(&view, name_offset, "reg"))
			{
				if (address_cells == 0U || address_cells > 2U ||
				    size_cells == 0U || size_cells > 2U)
				{
					return -1;
				}
				tuple_cells = address_cells + size_cells;
				if (property_length % (tuple_cells * 4U) != 0U)
				{
					return -1;
				}
				while (property_length >= tuple_cells * 4U)
				{
					if (arm64_boot_append_range(
					    info->memory, &info->memory_range_count,
					    ARM64_BOOT_MEMORY_RANGES_MAX,
					    arm64_fdt_read_cells(value,
								 address_cells),
					    arm64_fdt_read_cells(
						value + address_cells * 4U,
						size_cells)) != 0)
					{
						return -1;
					}
					value += tuple_cells * 4U;
					property_length -= tuple_cells * 4U;
				}
			}
			else if (framebuffer_depth == depth &&
				 arm64_fdt_prop_name_is(&view, name_offset, "reg"))
			{
				if (address_cells == 0U || address_cells > 2U ||
				    size_cells == 0U || size_cells > 2U)
				{
					return -1;
				}
				tuple_cells = address_cells + size_cells;
				if (property_length < tuple_cells * 4U)
				{
					return -1;
				}
				info->framebuffer.base =
				    arm64_fdt_read_cells(value, address_cells);
				info->framebuffer.size = arm64_fdt_read_cells(
				    value + address_cells * 4U, size_cells);
			}
			else if (framebuffer_depth == depth &&
				 property_length == 4U &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"width"))
			{
				info->framebuffer.width = arm64_fdt_read_be32(value);
			}
			else if (framebuffer_depth == depth &&
				 property_length == 4U &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"height"))
			{
				info->framebuffer.height =
				    arm64_fdt_read_be32(value);
			}
			else if (framebuffer_depth == depth &&
				 property_length == 4U &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"stride"))
			{
				info->framebuffer.pitch = arm64_fdt_read_be32(value);
			}
			else if (framebuffer_depth == depth &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"format"))
			{
				framebuffer_set_format(&info->framebuffer, value,
						       property_length);
			}
			else if (reserved_child_depth == depth &&
				 arm64_fdt_prop_name_is(&view, name_offset, "reg"))
			{
				if (reserved_address_cells == 0U ||
				    reserved_address_cells > 2U ||
				    reserved_size_cells == 0U ||
				    reserved_size_cells > 2U)
				{
					return -1;
				}
				tuple_cells =
				    reserved_address_cells + reserved_size_cells;
				if (property_length % (tuple_cells * 4U) != 0U)
				{
					return -1;
				}
				while (property_length >= tuple_cells * 4U)
				{
					if (arm64_boot_append_range(
					    info->reserved,
					    &info->reserved_range_count,
					    ARM64_BOOT_RESERVED_RANGES_MAX,
					    arm64_fdt_read_cells(
						value, reserved_address_cells),
					    arm64_fdt_read_cells(
						value +
						    reserved_address_cells * 4U,
						reserved_size_cells)) != 0)
					{
						return -1;
					}
					value += tuple_cells * 4U;
					property_length -= tuple_cells * 4U;
				}
			}
			else if (chosen_depth == depth &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"bootargs"))
			{
				uint32_t i;

				if (property_length == 0U ||
				    property_length > ARM64_BOOTARGS_MAX)
				{
					return -1;
				}
				for (i = 0U;
				     i < property_length && value[i] != 0U; i++)
				{
					info->bootargs[i] = (char)value[i];
				}
				if (i == property_length ||
				    i >= ARM64_BOOTARGS_MAX)
				{
					return -1;
				}
				info->bootargs[i] = '\0';
			}
			else if (chosen_depth == depth &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"linux,initrd-start"))
			{
				if (property_length != 4U &&
				    property_length != 8U)
				{
					return -1;
				}
				info->initrd_start = arm64_fdt_read_cells(
				    value, property_length / 4U);
			}
			else if (chosen_depth == depth &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"linux,initrd-end"))
			{
				if (property_length != 4U &&
				    property_length != 8U)
				{
					return -1;
				}
				info->initrd_end = arm64_fdt_read_cells(
				    value, property_length / 4U);
			}
			if (encoded_length >
			    UINT32_MAX - offset - 3U)
			{
				return -1;
			}
			offset = (offset + encoded_length + 3U) & ~3U;
		}
		else if (token == ARM64_FDT_NOP)
		{
			continue;
		}
		else if (token == ARM64_FDT_END)
		{
			return depth == 0U ? 0 : -1;
		}
		else
		{
			return -1;
		}
	}
	return -1;
}
