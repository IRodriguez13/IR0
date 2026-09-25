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
#define ARM64_FRAME_SIZE 4096U
#define FDT_NODE_DEPTH_MAX 32U

/* Written by the stackless entry assembly before x0 is reused. */
uintptr_t arm64_firmware_fdt;

static struct arm64_board_boot_info g_boot_info;
static int g_memory_finalized;

static uint64_t min_u64(uint64_t a, uint64_t b)
{
	return a < b ? a : b;
}

static uint64_t max_u64(uint64_t a, uint64_t b)
{
	return a > b ? a : b;
}

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

static int node_name_is(const volatile uint8_t *name, uint32_t available,
			const char *wanted)
{
	uint32_t i = 0;

	while (wanted[i] && i < available)
	{
		if (name[i] != (uint8_t)wanted[i])
			return 0;
		i++;
	}
	return wanted[i] == '\0' && i == available;
}

static int node_has_prefix(const volatile uint8_t *name, uint32_t available,
			   const char *prefix)
{
	uint32_t i = 0;

	while (prefix[i])
	{
		if (i >= available || name[i] != (uint8_t)prefix[i])
			return 0;
		i++;
	}
	return 1;
}

static int node_is_memory(const volatile uint8_t *name, uint32_t available)
{
	return node_name_is(name, available, "memory") ||
	       node_has_prefix(name, available, "memory@");
}

static int append_range(struct ir0_phys_range *ranges, uint32_t *count,
			uint32_t maximum, uint64_t base, uint64_t size)
{
	if (size == 0 || base + size < base)
		return 0;
	if (*count >= maximum)
		return -1;
	ranges[*count].base = base;
	ranges[*count].size = size;
	(*count)++;
	return 0;
}

static int ranges_overlap(const struct ir0_phys_range *a,
			  const struct ir0_phys_range *b)
{
	return a->base < b->base + b->size && b->base < a->base + a->size;
}

static int append_usable(uint64_t base, uint64_t end)
{
	struct ir0_phys_range *range;

	if (end <= base)
		return 0;
	if (g_boot_info.usable_range_count >= ARM64_BOOT_USABLE_RANGES_MAX)
		return -1;
	range = &g_boot_info.usable[g_boot_info.usable_range_count++];
	range->base = base;
	range->size = end - base;
	return 0;
}

static int build_usable_ranges(void)
{
	uint32_t memory_index;

	g_boot_info.usable_range_count = 0;
	g_memory_finalized = 0;
	for (memory_index = 0; memory_index < g_boot_info.memory_range_count;
	     memory_index++)
	{
		struct ir0_phys_range current[ARM64_BOOT_USABLE_RANGES_MAX];
		struct ir0_phys_range next[ARM64_BOOT_USABLE_RANGES_MAX];
		uint32_t current_count = 1;
		uint32_t reserved_index;
		uint64_t memory_start = g_boot_info.memory[memory_index].base;
		uint64_t memory_end = memory_start +
				      g_boot_info.memory[memory_index].size;

		if (memory_start > UINT64_MAX - (ARM64_FRAME_SIZE - 1U))
			continue;
		memory_start = (memory_start + ARM64_FRAME_SIZE - 1U) &
			       ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
		memory_end &= ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
		if (memory_end <= memory_start)
			continue;
		current[0].base = memory_start;
		current[0].size = memory_end - memory_start;

		for (reserved_index = 0;
		     reserved_index < g_boot_info.reserved_range_count;
		     reserved_index++)
		{
			uint32_t i;
			uint32_t next_count = 0;
			uint64_t reserved_start = g_boot_info.reserved[reserved_index].base;
			uint64_t reserved_end = reserved_start +
						g_boot_info.reserved[reserved_index].size;

			reserved_start &= ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
			if ((reserved_end & (ARM64_FRAME_SIZE - 1U)) != 0U)
			{
				if (reserved_end > UINT64_MAX - (ARM64_FRAME_SIZE - 1U))
					reserved_end = UINT64_MAX;
				else
					reserved_end = (reserved_end + ARM64_FRAME_SIZE - 1U) &
						       ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
			}

			for (i = 0; i < current_count; i++)
			{
				uint64_t start = current[i].base;
				uint64_t end = start + current[i].size;
				uint64_t cut_start = max_u64(start, reserved_start);
				uint64_t cut_end = min_u64(end, reserved_end);

				if (cut_start >= cut_end)
				{
					if (next_count >= ARM64_BOOT_USABLE_RANGES_MAX)
						return -1;
					next[next_count++] = current[i];
					continue;
				}
				if (start < cut_start)
				{
					if (next_count >= ARM64_BOOT_USABLE_RANGES_MAX)
						return -1;
					next[next_count].base = start;
					next[next_count++].size = cut_start - start;
				}
				if (cut_end < end)
				{
					if (next_count >= ARM64_BOOT_USABLE_RANGES_MAX)
						return -1;
					next[next_count].base = cut_end;
					next[next_count++].size = end - cut_end;
				}
			}
			current_count = next_count;
			for (i = 0; i < current_count; i++)
				current[i] = next[i];
		}

		for (reserved_index = 0; reserved_index < current_count;
		     reserved_index++)
		{
			if (append_usable(current[reserved_index].base,
					  current[reserved_index].base +
					  current[reserved_index].size) != 0)
				return -1;
		}
	}
	return 0;
}

static int parse_reservation_map(const volatile uint8_t *fdt, uint32_t total)
{
	uint32_t off = read_be32(fdt + 16);

	if (off < FDT_HEADER_SIZE || off > total)
		return -1;
	while (off + 16U <= total)
	{
		uint64_t base = read_cells(fdt + off, 2U);
		uint64_t size = read_cells(fdt + off + 8U, 2U);

		off += 16U;
		if (base == 0 && size == 0)
			return 0;
		if (append_range(g_boot_info.reserved,
				 &g_boot_info.reserved_range_count,
				 ARM64_BOOT_RESERVED_RANGES_MAX, base, size) != 0)
			return -1;
	}
	return -1;
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

static int string_list_has(const volatile uint8_t *value, uint32_t len,
			   const char *wanted)
{
	uint32_t off = 0;

	while (off < len)
	{
		uint32_t item_len = 0;

		while (off + item_len < len && value[off + item_len] != 0)
			item_len++;
		if (off + item_len >= len)
			return 0;
		if (node_name_is(value + off, item_len, wanted))
			return 1;
		off += item_len + 1U;
	}
	return 0;
}

static int string_list_has_prefix(const volatile uint8_t *value, uint32_t len,
				  const char *wanted)
{
	uint32_t off = 0;

	while (off < len)
	{
		uint32_t item_len = 0;

		while (off + item_len < len && value[off + item_len] != 0)
			item_len++;
		if (off + item_len >= len)
			return 0;
		if (node_has_prefix(value + off, item_len, wanted))
			return 1;
		off += item_len + 1U;
	}
	return 0;
}

static int parse_platform_tree(const volatile uint8_t *fdt, uint32_t total)
{
	const volatile uint8_t *structure;
	const volatile uint8_t *strings;
	uint32_t struct_off = read_be32(fdt + 8);
	uint32_t strings_off = read_be32(fdt + 12);
	uint32_t strings_size = read_be32(fdt + 32);
	uint32_t struct_size = read_be32(fdt + 36);
	uint32_t address_cells = 2;
	uint32_t size_cells = 1;
	uint32_t reserved_address_cells = 2;
	uint32_t reserved_size_cells = 1;
	uint32_t off = 0;
	uint32_t depth = 0;
	uint32_t memory_depth = 0;
	uint32_t cpus_depth = 0;
	uint32_t psci_depth = 0;
	uint32_t reserved_depth = 0;
	uint32_t reserved_child_depth = 0;

	if (struct_off < FDT_HEADER_SIZE || strings_off < FDT_HEADER_SIZE ||
	    struct_off > total || struct_size > total - struct_off ||
	    strings_off > total || strings_size > total - strings_off)
		return -1;
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
				return -1;
			depth++;
			if (depth == 2U &&
			    node_is_memory(structure + name_start, off - name_start))
				memory_depth = depth;
			if (depth == 2U &&
			    node_name_is(structure + name_start, off - name_start, "cpus"))
				cpus_depth = depth;
			else if (cpus_depth != 0U && depth == cpus_depth + 1U &&
				 node_has_prefix(structure + name_start, off - name_start,
						 "cpu@"))
				g_boot_info.cpu_count++;
			if (node_name_is(structure + name_start, off - name_start, "psci"))
				psci_depth = depth;
			if (depth == 2U &&
			    node_name_is(structure + name_start, off - name_start,
					 "reserved-memory"))
				reserved_depth = depth;
			else if (reserved_depth != 0U && depth == reserved_depth + 1U)
				reserved_child_depth = depth;
			off = (off + 4U) & ~3U;
		}
		else if (token == FDT_END_NODE)
		{
			if (memory_depth == depth)
				memory_depth = 0;
			if (reserved_child_depth == depth)
				reserved_child_depth = 0;
			if (cpus_depth == depth)
				cpus_depth = 0;
			if (psci_depth == depth)
				psci_depth = 0;
			if (reserved_depth == depth)
				reserved_depth = 0;
			if (depth == 0)
				return -1;
			depth--;
		}
		else if (token == FDT_PROP)
		{
			uint32_t len;
			uint32_t nameoff;
			const volatile uint8_t *value;
			uint32_t tuple_cells;

			if (off + 8U > struct_size)
				return -1;
			len = read_be32(structure + off);
			nameoff = read_be32(structure + off + 4U);
			off += 8U;
			if (len > struct_size - off)
				return -1;
			value = structure + off;
			if (depth == 1U && len >= 4U &&
			    prop_name_is(strings, strings_size, nameoff, "#address-cells"))
				address_cells = read_be32(value);
			else if (depth == 1U && len >= 4U &&
				 prop_name_is(strings, strings_size, nameoff, "#size-cells"))
				size_cells = read_be32(value);
			else if (reserved_depth == depth && len >= 4U &&
				 prop_name_is(strings, strings_size, nameoff, "#address-cells"))
				reserved_address_cells = read_be32(value);
			else if (reserved_depth == depth && len >= 4U &&
				 prop_name_is(strings, strings_size, nameoff, "#size-cells"))
				reserved_size_cells = read_be32(value);
			else if (memory_depth == depth &&
				 prop_name_is(strings, strings_size, nameoff, "reg") &&
				 address_cells > 0U && address_cells <= 2U &&
				 size_cells > 0U && size_cells <= 2U)
			{
				tuple_cells = address_cells + size_cells;
				if ((len % (tuple_cells * 4U)) != 0U)
					return -1;
				while (len >= tuple_cells * 4U)
				{
					if (append_range(g_boot_info.memory,
							 &g_boot_info.memory_range_count,
							 ARM64_BOOT_MEMORY_RANGES_MAX,
							 read_cells(value, address_cells),
							 read_cells(value + address_cells * 4U,
								    size_cells)) != 0)
						return -1;
					value += tuple_cells * 4U;
					len -= tuple_cells * 4U;
				}
			}
			else if (reserved_child_depth == depth &&
				 prop_name_is(strings, strings_size, nameoff, "reg") &&
				 reserved_address_cells > 0U && reserved_address_cells <= 2U &&
				 reserved_size_cells > 0U && reserved_size_cells <= 2U)
			{
				tuple_cells = reserved_address_cells + reserved_size_cells;
				if ((len % (tuple_cells * 4U)) != 0U)
					return -1;
				while (len >= tuple_cells * 4U)
				{
					if (append_range(g_boot_info.reserved,
							 &g_boot_info.reserved_range_count,
							 ARM64_BOOT_RESERVED_RANGES_MAX,
							 read_cells(value, reserved_address_cells),
							 read_cells(value + reserved_address_cells * 4U,
								    reserved_size_cells)) != 0)
						return -1;
					value += tuple_cells * 4U;
					len -= tuple_cells * 4U;
				}
			}
			else if (prop_name_is(strings, strings_size, nameoff, "compatible"))
			{
				if (string_list_has(value, len, "arm,gic-v3"))
					g_boot_info.irq_controller = ARM64_IRQ_CONTROLLER_GIC_V3;
				else if (string_list_has(value, len, "arm,gic-400") ||
					 string_list_has(value, len, "arm,cortex-a15-gic"))
					g_boot_info.irq_controller = ARM64_IRQ_CONTROLLER_GIC_V2;
				if (string_list_has(value, len, "arm,armv8-timer"))
					g_boot_info.architected_timer = 1;
				if (string_list_has_prefix(value, len, "raspberrypi,rp1-"))
					g_boot_info.rp1_present = 1;
			}
			else if (psci_depth == depth &&
				 prop_name_is(strings, strings_size, nameoff, "method"))
			{
				if (string_list_has(value, len, "hvc"))
					g_boot_info.psci_conduit = ARM64_PSCI_CONDUIT_HVC;
				else if (string_list_has(value, len, "smc"))
					g_boot_info.psci_conduit = ARM64_PSCI_CONDUIT_SMC;
			}
			off = (off + read_be32(structure + off - 8U) + 3U) & ~3U;
		}
		else if (token == FDT_NOP)
		{
			continue;
		}
		else if (token == FDT_END)
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

struct fdt_irq_node
{
	uint32_t child_address_cells;
	uint32_t child_size_cells;
	uint32_t reg_address_cells;
	uint32_t reg_size_cells;
	enum arm64_irq_controller_model model;
	const volatile uint8_t *reg_value;
	uint32_t reg_len;
	const volatile uint8_t *ranges_value;
	uint32_t ranges_len;
	int ranges_present;
	int pl011;
	int disabled;
	uint32_t range_count;
	struct ir0_phys_range range[2];
};

static int translate_irq_address(const struct fdt_irq_node *nodes,
				 uint32_t node_index, uint64_t *address,
				 uint64_t size)
{
	uint32_t bus_index = node_index;
	uint64_t current = *address;

	while (bus_index > 0U)
	{
		const struct fdt_irq_node *bus = &nodes[bus_index - 1U];
		const volatile uint8_t *value = bus->ranges_value;
		uint32_t len = bus->ranges_len;
		uint32_t tuple_cells;
		int matched = 0;

		/* The root already uses CPU physical addresses. */
		if (bus_index == 1U)
			break;
		if (!bus->ranges_present)
			return -1;
		if (len == 0U)
		{
			bus_index--;
			continue;
		}
		if (bus->child_address_cells == 0U || bus->child_address_cells > 2U ||
		    bus->reg_address_cells == 0U || bus->reg_address_cells > 2U ||
		    bus->child_size_cells == 0U || bus->child_size_cells > 2U)
			return -1;
		tuple_cells = bus->child_address_cells + bus->reg_address_cells +
			      bus->child_size_cells;
		if (tuple_cells > UINT32_MAX / 4U ||
		    (len % (tuple_cells * 4U)) != 0U)
			return -1;
		while (len >= tuple_cells * 4U)
		{
			uint64_t child = read_cells(value, bus->child_address_cells);
			uint64_t parent = read_cells(
				value + bus->child_address_cells * 4U,
				bus->reg_address_cells);
			uint64_t span = read_cells(
				value + (bus->child_address_cells +
					 bus->reg_address_cells) * 4U,
				bus->child_size_cells);

			if (span != 0U && current >= child && current - child < span &&
			    size <= span - (current - child))
			{
				uint64_t offset = current - child;

				if (parent > UINT64_MAX - offset)
					return -1;
				current = parent + offset;
				matched = 1;
				break;
			}
			value += tuple_cells * 4U;
			len -= tuple_cells * 4U;
		}
		if (!matched)
			return -1;
		bus_index--;
	}
	*address = current;
	return 0;
}

static int parse_irq_resources(const volatile uint8_t *fdt, uint32_t total)
{
	const volatile uint8_t *structure;
	const volatile uint8_t *strings;
	struct fdt_irq_node nodes[FDT_NODE_DEPTH_MAX];
	uint32_t struct_off = read_be32(fdt + 8);
	uint32_t strings_off = read_be32(fdt + 12);
	uint32_t strings_size = read_be32(fdt + 32);
	uint32_t struct_size = read_be32(fdt + 36);
	uint32_t depth = 0;
	uint32_t off = 0;

	if (struct_off < FDT_HEADER_SIZE || strings_off < FDT_HEADER_SIZE ||
	    struct_off > total || struct_size > total - struct_off ||
	    strings_off > total || strings_size > total - strings_off)
		return -1;
	structure = fdt + struct_off;
	strings = fdt + strings_off;

	while (off + 4U <= struct_size)
	{
		uint32_t token = read_be32(structure + off);

		off += 4U;
		if (token == FDT_BEGIN_NODE)
		{
			struct fdt_irq_node *node;
			uint32_t parent_address_cells = 2U;
			uint32_t parent_size_cells = 1U;

			if (depth >= FDT_NODE_DEPTH_MAX)
				return -1;
			if (depth > 0U)
			{
				parent_address_cells = nodes[depth - 1U].child_address_cells;
				parent_size_cells = nodes[depth - 1U].child_size_cells;
			}
			node = &nodes[depth++];
			node->child_address_cells = parent_address_cells;
			node->child_size_cells = parent_size_cells;
			node->reg_address_cells = parent_address_cells;
			node->reg_size_cells = parent_size_cells;
			node->model = ARM64_IRQ_CONTROLLER_UNKNOWN;
			node->reg_value = NULL;
			node->reg_len = 0U;
			node->ranges_value = NULL;
			node->ranges_len = 0U;
			node->ranges_present = 0;
			node->pl011 = 0;
			node->disabled = 0;
			node->range_count = 0U;
			while (off < struct_size && structure[off] != 0)
				off++;
			if (off >= struct_size)
				return -1;
			off = (off + 4U) & ~3U;
		}
		else if (token == FDT_END_NODE)
		{
			struct fdt_irq_node *node;

			if (depth == 0U)
				return -1;
			node = &nodes[depth - 1U];
			if (node->pl011 && !node->disabled &&
			    g_boot_info.console_uart == ARM64_UART_UNKNOWN)
			{
				uint32_t tuple_cells = node->reg_address_cells +
						       node->reg_size_cells;
				if (!node->reg_value || node->reg_address_cells == 0U ||
				    node->reg_address_cells > 2U || node->reg_size_cells == 0U ||
				    node->reg_size_cells > 2U || node->reg_len < tuple_cells * 4U)
					return -1;
				g_boot_info.console_mmio.base =
					read_cells(node->reg_value, node->reg_address_cells);
				g_boot_info.console_mmio.size = read_cells(
					node->reg_value + node->reg_address_cells * 4U,
					node->reg_size_cells);
				if (g_boot_info.console_mmio.size == 0U ||
				    translate_irq_address(nodes, depth - 1U,
					&g_boot_info.console_mmio.base,
					g_boot_info.console_mmio.size) != 0)
					return -1;
				g_boot_info.console_uart = ARM64_UART_PL011;
			}
			if (node->model != ARM64_IRQ_CONTROLLER_UNKNOWN)
			{
				uint32_t i;
				uint32_t tuple_cells = node->reg_address_cells +
						       node->reg_size_cells;
				uint32_t len = node->reg_len;
				const volatile uint8_t *value = node->reg_value;

				if (!value || node->reg_address_cells == 0U ||
				    node->reg_address_cells > 2U ||
				    node->reg_size_cells == 0U ||
				    node->reg_size_cells > 2U || tuple_cells > UINT32_MAX / 4U ||
				    (len % (tuple_cells * 4U)) != 0U)
					return -1;
				while (len >= tuple_cells * 4U && node->range_count < 2U)
				{
					struct ir0_phys_range *range =
						&node->range[node->range_count++];

					range->base = read_cells(value, node->reg_address_cells);
					range->size = read_cells(
						value + node->reg_address_cells * 4U,
						node->reg_size_cells);
					if (range->size == 0U || range->base + range->size < range->base)
						return -1;
					if (translate_irq_address(nodes, depth - 1U, &range->base,
								  range->size) != 0)
						return -1;
					value += tuple_cells * 4U;
					len -= tuple_cells * 4U;
				}

				if (node->range_count < 2U ||
				    (g_boot_info.irq_controller != ARM64_IRQ_CONTROLLER_UNKNOWN &&
				     g_boot_info.irq_controller != node->model))
					return -1;
				g_boot_info.irq_controller = node->model;
				g_boot_info.irq_range_count = node->range_count;
				for (i = 0; i < node->range_count; i++)
					g_boot_info.irq_mmio[i] = node->range[i];
			}
			depth--;
		}
		else if (token == FDT_PROP)
		{
			struct fdt_irq_node *node;
			const volatile uint8_t *value;
			uint32_t len;
			uint32_t nameoff;

			if (depth == 0U || off + 8U > struct_size)
				return -1;
			len = read_be32(structure + off);
			nameoff = read_be32(structure + off + 4U);
			off += 8U;
			if (len > struct_size - off)
				return -1;
			value = structure + off;
			node = &nodes[depth - 1U];
			if (len >= 4U &&
			    prop_name_is(strings, strings_size, nameoff, "#address-cells"))
				node->child_address_cells = read_be32(value);
			else if (len >= 4U &&
				 prop_name_is(strings, strings_size, nameoff, "#size-cells"))
				node->child_size_cells = read_be32(value);
			else if (prop_name_is(strings, strings_size, nameoff, "compatible"))
			{
				if (string_list_has(value, len, "arm,gic-v3"))
					node->model = ARM64_IRQ_CONTROLLER_GIC_V3;
				else if (string_list_has(value, len, "arm,gic-400") ||
					 string_list_has(value, len, "arm,cortex-a15-gic"))
					node->model = ARM64_IRQ_CONTROLLER_GIC_V2;
				if (string_list_has(value, len, "arm,pl011"))
					node->pl011 = 1;
			}
			else if (prop_name_is(strings, strings_size, nameoff, "status") &&
				 string_list_has(value, len, "disabled"))
				node->disabled = 1;
			else if (prop_name_is(strings, strings_size, nameoff, "reg"))
			{
				node->reg_value = value;
				node->reg_len = len;
			}
			else if (prop_name_is(strings, strings_size, nameoff, "ranges"))
			{
				node->ranges_value = value;
				node->ranges_len = len;
				node->ranges_present = 1;
			}
			off = (off + read_be32(structure + off - 8U) + 3U) & ~3U;
		}
		else if (token == FDT_NOP)
			continue;
		else if (token == FDT_END)
			return depth == 0U ? 0 : -1;
		else
			return -1;
	}
	return -1;
}

int arm64_fdt_boot_info_init(uintptr_t fdt_pa)
{
	const volatile uint8_t *header;
	uint32_t size;

	g_boot_info.fdt_pa = fdt_pa;
	g_boot_info.fdt_size = 0;
	g_boot_info.fdt_magic = 0;
	g_boot_info.fdt_valid = 0;
	g_boot_info.cpu_count = 0;
	g_boot_info.irq_controller = ARM64_IRQ_CONTROLLER_UNKNOWN;
	g_boot_info.irq_range_count = 0U;
	g_boot_info.psci_conduit = ARM64_PSCI_CONDUIT_UNKNOWN;
	g_boot_info.architected_timer = 0;
	g_boot_info.rp1_present = 0;
	g_boot_info.console_uart = ARM64_UART_UNKNOWN;
	g_boot_info.console_mmio.base = 0U;
	g_boot_info.console_mmio.size = 0U;
	g_boot_info.memory_range_count = 0;
	g_boot_info.reserved_range_count = 0;
	g_boot_info.usable_range_count = 0;

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

	if (parse_reservation_map(header, size) != 0 ||
	    parse_platform_tree(header, size) != 0 ||
	    parse_irq_resources(header, size) != 0)
		return -1;
	g_boot_info.fdt_valid = 1;
	return 0;
}

const struct arm64_board_boot_info *arm64_board_boot_info(void)
{
	return &g_boot_info;
}

int arm64_board_finalize_memory(uintptr_t kernel_base, size_t kernel_size)
{
	uint32_t i;
	struct ir0_phys_range kernel;

	if (!g_boot_info.fdt_valid || kernel_size == 0U ||
	    (uint64_t)kernel_base + (uint64_t)kernel_size < (uint64_t)kernel_base)
		return -1;
	if (g_memory_finalized)
		return 0;
	if (g_boot_info.reserved_range_count > ARM64_BOOT_RESERVED_RANGES_MAX - 2U)
		return -1;

	kernel.base = (uint64_t)kernel_base;
	kernel.size = (uint64_t)kernel_size;
	for (i = 0; i < g_boot_info.memory_range_count; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < g_boot_info.memory_range_count; j++)
		{
			if (ranges_overlap(&g_boot_info.memory[i], &g_boot_info.memory[j]))
				return -1;
		}
	}
	if (append_range(g_boot_info.reserved, &g_boot_info.reserved_range_count,
			 ARM64_BOOT_RESERVED_RANGES_MAX, g_boot_info.fdt_pa,
			 g_boot_info.fdt_size) != 0 ||
	    append_range(g_boot_info.reserved, &g_boot_info.reserved_range_count,
			 ARM64_BOOT_RESERVED_RANGES_MAX, kernel.base,
			 kernel.size) != 0)
		return -1;
	if (build_usable_ranges() != 0)
		return -1;
	g_memory_finalized = 1;
	return 0;
}
