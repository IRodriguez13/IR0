/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_info_devices.c
 * Description: ARM64 FDT device, console, and interrupt resource parsing.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "boot_info_private.h"

#include <stddef.h>
#include <stdint.h>

struct arm64_fdt_device_node
{
	uint32_t child_address_cells;
	uint32_t child_size_cells;
	uint32_t reg_address_cells;
	uint32_t reg_size_cells;
	enum arm64_irq_controller_model irq_model;
	const volatile uint8_t *reg_value;
	uint32_t reg_length;
	const volatile uint8_t *ranges_value;
	uint32_t ranges_length;
	int ranges_present;
	int cpus;
	int cpu;
	int psci;
	int pl011;
	int pl031;
	int genet;
	int disabled;
	const volatile uint8_t *interrupts_value;
	uint32_t interrupts_length;
	const volatile uint8_t *mac_value;
	uint32_t mac_length;
	char phy_mode[16];
	uint32_t phy_addr;
};

static void set_hypervisor(struct arm64_board_boot_info *info,
			   enum arm64_hypervisor_kind kind,
			   const char *vendor)
{
	size_t i;

	info->hypervisor_kind = kind;
	if (vendor == NULL)
	{
		info->hypervisor_vendor[0] = '\0';
		return;
	}
	for (i = 0U; i < sizeof(info->hypervisor_vendor) - 1U &&
	     vendor[i] != '\0'; i++)
	{
		info->hypervisor_vendor[i] = vendor[i];
	}
	info->hypervisor_vendor[i] = '\0';
}

static void note_hypervisor_compatible(struct arm64_board_boot_info *info,
				       const volatile uint8_t *value,
				       uint32_t length)
{
	if (arm64_fdt_string_list_has(value, length, "linux,kvm") ||
	    arm64_fdt_string_list_has(value, length, "arm,kvm"))
	{
		set_hypervisor(info, ARM64_HV_KVM, "KVMKVMKVM");
	}
	else if (arm64_fdt_string_list_has(value, length, "xen,xen") ||
		 arm64_fdt_string_list_has(value, length, "arm,xen") ||
		 arm64_fdt_string_list_has(value, length, "xen,arm"))
	{
		set_hypervisor(info, ARM64_HV_XEN, "XenVMMXenVMM");
	}
	else if (arm64_fdt_string_list_has(
		     value, length, "microsoft,hyperv-genic") ||
		 arm64_fdt_string_list_has(value, length,
					   "microsoft,hyperv"))
	{
		set_hypervisor(info, ARM64_HV_HYPERV, "Microsoft Hv");
	}
	else if (arm64_fdt_string_list_has(value, length,
					   "vmware,vmvisor") ||
		 arm64_fdt_string_list_has(value, length, "vmware,GuestVM"))
	{
		set_hypervisor(info, ARM64_HV_VMWARE, "VMwareVMware");
	}
	else if ((arm64_fdt_string_list_has(value, length, "qemu,virt") ||
		  arm64_fdt_string_list_has_prefix(value, length, "qemu,")) &&
		 info->hypervisor_kind == ARM64_HV_NONE)
	{
		set_hypervisor(info, ARM64_HV_QEMU, "TCGTCGTCGTCG");
	}
}

static int translate_address(const struct arm64_fdt_device_node *nodes,
			     uint32_t node_index, uint64_t *address,
			     uint64_t size)
{
	uint32_t bus_index = node_index;
	uint64_t current = *address;

	while (bus_index > 0U)
	{
		const struct arm64_fdt_device_node *bus =
		    &nodes[bus_index - 1U];
		const volatile uint8_t *value = bus->ranges_value;
		uint32_t length = bus->ranges_length;
		uint32_t tuple_cells;
		int matched = 0;

		if (bus_index == 1U)
		{
			break;
		}
		if (!bus->ranges_present)
		{
			return -1;
		}
		if (length == 0U)
		{
			bus_index--;
			continue;
		}
		if (bus->child_address_cells == 0U ||
		    bus->child_address_cells > 2U ||
		    bus->reg_address_cells == 0U ||
		    bus->reg_address_cells > 2U ||
		    bus->child_size_cells == 0U ||
		    bus->child_size_cells > 2U)
		{
			return -1;
		}
		tuple_cells = bus->child_address_cells +
			      bus->reg_address_cells + bus->child_size_cells;
		if (tuple_cells > UINT32_MAX / 4U ||
		    length % (tuple_cells * 4U) != 0U)
		{
			return -1;
		}
		while (length >= tuple_cells * 4U)
		{
			uint64_t child = arm64_fdt_read_cells(
			    value, bus->child_address_cells);
			uint64_t parent = arm64_fdt_read_cells(
			    value + bus->child_address_cells * 4U,
			    bus->reg_address_cells);
			uint64_t span = arm64_fdt_read_cells(
			    value + (bus->child_address_cells +
				     bus->reg_address_cells) *
					4U,
			    bus->child_size_cells);

			if (span != 0U && current >= child &&
			    current - child < span &&
			    size <= span - (current - child))
			{
				uint64_t child_offset = current - child;

				if (parent > UINT64_MAX - child_offset)
				{
					return -1;
				}
				current = parent + child_offset;
				matched = 1;
				break;
			}
			value += tuple_cells * 4U;
			length -= tuple_cells * 4U;
		}
		if (!matched)
		{
			return -1;
		}
		bus_index--;
	}
	*address = current;
	return 0;
}

static int finish_console(struct arm64_board_boot_info *info,
			  const struct arm64_fdt_device_node *nodes,
			  uint32_t depth)
{
	const struct arm64_fdt_device_node *node = &nodes[depth - 1U];
	uint32_t tuple_cells;

	if (!node->pl011 || node->disabled ||
	    info->console_uart != ARM64_UART_UNKNOWN)
	{
		return 0;
	}
	tuple_cells = node->reg_address_cells + node->reg_size_cells;
	if (node->reg_value == NULL || node->reg_address_cells == 0U ||
	    node->reg_address_cells > 2U || node->reg_size_cells == 0U ||
	    node->reg_size_cells > 2U ||
	    tuple_cells > UINT32_MAX / 4U ||
	    node->reg_length < tuple_cells * 4U)
	{
		return -1;
	}
	info->console_mmio.base =
	    arm64_fdt_read_cells(node->reg_value, node->reg_address_cells);
	info->console_mmio.size = arm64_fdt_read_cells(
	    node->reg_value + node->reg_address_cells * 4U,
	    node->reg_size_cells);
	if (info->console_mmio.size == 0U ||
	    translate_address(nodes, depth - 1U, &info->console_mmio.base,
			      info->console_mmio.size) != 0)
	{
		return -1;
	}
	info->console_uart = ARM64_UART_PL011;
	return 0;
}

/*
 * First enabled "arm,pl031" node: the wall-clock source. A malformed node
 * only leaves the board without an RTC; it never fails the boot parse.
 */
static int finish_rtc(struct arm64_board_boot_info *info,
		      const struct arm64_fdt_device_node *nodes, uint32_t depth)
{
	const struct arm64_fdt_device_node *node = &nodes[depth - 1U];
	uint32_t tuple_cells;

	if (!node->pl031 || node->disabled || info->rtc_mmio.size != 0U)
		return 0;
	tuple_cells = node->reg_address_cells + node->reg_size_cells;
	if (node->reg_value == NULL || node->reg_address_cells == 0U ||
	    node->reg_address_cells > 2U || node->reg_size_cells == 0U ||
	    node->reg_size_cells > 2U || node->reg_length < tuple_cells * 4U)
		return 0;
	info->rtc_mmio.base =
	    arm64_fdt_read_cells(node->reg_value, node->reg_address_cells);
	info->rtc_mmio.size = arm64_fdt_read_cells(
	    node->reg_value + node->reg_address_cells * 4U,
	    node->reg_size_cells);
	if (translate_address(nodes, depth - 1U, &info->rtc_mmio.base,
			      info->rtc_mmio.size) != 0)
		info->rtc_mmio.size = 0U;
	return 0;
}

static int finish_irq_controller(struct arm64_board_boot_info *info,
				 const struct arm64_fdt_device_node *nodes,
				 uint32_t depth)
{
	const struct arm64_fdt_device_node *node = &nodes[depth - 1U];
	const volatile uint8_t *value = node->reg_value;
	uint32_t tuple_cells;
	uint32_t range_count = 0U;
	uint32_t length = node->reg_length;

	if (node->irq_model == ARM64_IRQ_CONTROLLER_UNKNOWN)
	{
		return 0;
	}
	tuple_cells = node->reg_address_cells + node->reg_size_cells;
	if (value == NULL || node->reg_address_cells == 0U ||
	    node->reg_address_cells > 2U || node->reg_size_cells == 0U ||
	    node->reg_size_cells > 2U || tuple_cells > UINT32_MAX / 4U ||
	    length % (tuple_cells * 4U) != 0U)
	{
		return -1;
	}
	while (length >= tuple_cells * 4U && range_count < 2U)
	{
		struct ir0_phys_range *range = &info->irq_mmio[range_count];

		range->base =
		    arm64_fdt_read_cells(value, node->reg_address_cells);
		range->size = arm64_fdt_read_cells(
		    value + node->reg_address_cells * 4U,
		    node->reg_size_cells);
		if (range->size == 0U ||
		    range->base > UINT64_MAX - range->size ||
		    translate_address(nodes, depth - 1U, &range->base,
				      range->size) != 0)
		{
			return -1;
		}
		range_count++;
		value += tuple_cells * 4U;
		length -= tuple_cells * 4U;
	}
	if (range_count == 0U ||
	    (node->irq_model != ARM64_IRQ_CONTROLLER_BCM2836_LOCAL &&
	     range_count < 2U) ||
	    (info->irq_controller != ARM64_IRQ_CONTROLLER_UNKNOWN &&
	     info->irq_controller != node->irq_model))
	{
		return -1;
	}
	info->irq_controller = node->irq_model;
	info->irq_range_count = range_count;
	return 0;
}

static int finish_genet(struct arm64_board_boot_info *info,
			const struct arm64_fdt_device_node *nodes,
			uint32_t depth)
{
	const struct arm64_fdt_device_node *node = &nodes[depth - 1U];
	uint32_t tuple_cells;
	uint32_t i;

	if (!node->genet || node->disabled || info->genet_present)
		return 0;
	tuple_cells = node->reg_address_cells + node->reg_size_cells;
	if (node->reg_value == NULL || node->reg_address_cells == 0U ||
	    node->reg_address_cells > 2U || node->reg_size_cells == 0U ||
	    node->reg_size_cells > 2U || tuple_cells > UINT32_MAX / 4U ||
	    node->reg_length < tuple_cells * 4U)
		return -1;
	info->genet_mmio.base =
	    arm64_fdt_read_cells(node->reg_value, node->reg_address_cells);
	info->genet_mmio.size = arm64_fdt_read_cells(
	    node->reg_value + node->reg_address_cells * 4U,
	    node->reg_size_cells);
	if (info->genet_mmio.size == 0U ||
	    translate_address(nodes, depth - 1U, &info->genet_mmio.base,
			      info->genet_mmio.size) != 0)
		return -1;
	if (node->interrupts_value && node->interrupts_length >= 24U)
	{
		for (i = 0; i < 2U && (i + 1U) * 12U <= node->interrupts_length;
		     i++)
		{
			uint32_t type = arm64_fdt_read_be32(
			    node->interrupts_value + i * 12U);
			uint32_t num = arm64_fdt_read_be32(
			    node->interrupts_value + i * 12U + 4U);

			if (type == 0U && num < 988U)
				info->genet_irq[i] = num + 32U;
			else if (type == 1U && num < 16U)
				info->genet_irq[i] = num + 16U;
			else
				return -1;
			info->genet_irq_count++;
		}
	}
	if (node->mac_value && node->mac_length >= 6U)
	{
		for (i = 0; i < 6U; i++)
			info->genet_mac[i] = node->mac_value[i];
	}
	for (i = 0; i < sizeof(info->genet_phy_mode) - 1U &&
	     node->phy_mode[i] != '\0'; i++)
		info->genet_phy_mode[i] = node->phy_mode[i];
	info->genet_phy_mode[i] = '\0';
	info->genet_phy_addr = node->phy_addr ? node->phy_addr : 1U;
	info->genet_present = 1;
	return 0;
}

int arm64_boot_parse_devices(struct arm64_boot_parse_context *context,
			     const volatile uint8_t *fdt, uint32_t total)
{
	struct arm64_fdt_device_node nodes[ARM64_FDT_NODE_DEPTH_MAX];
	struct arm64_fdt_view view;
	struct arm64_board_boot_info *info;
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
			struct arm64_fdt_device_node *node;
			uint32_t name_start = offset;
			uint32_t parent_address_cells = 2U;
			uint32_t parent_size_cells = 1U;

			if (depth >= ARM64_FDT_NODE_DEPTH_MAX)
			{
				return -1;
			}
			while (offset < view.structure_size &&
			       view.structure[offset] != 0U)
			{
				offset++;
			}
			if (offset >= view.structure_size)
			{
				return -1;
			}
			if (depth > 0U)
			{
				parent_address_cells =
				    nodes[depth - 1U].child_address_cells;
				parent_size_cells =
				    nodes[depth - 1U].child_size_cells;
			}
			node = &nodes[depth];
			node->child_address_cells = parent_address_cells;
			node->child_size_cells = parent_size_cells;
			node->reg_address_cells = parent_address_cells;
			node->reg_size_cells = parent_size_cells;
			node->irq_model = ARM64_IRQ_CONTROLLER_UNKNOWN;
			node->reg_value = NULL;
			node->reg_length = 0U;
			node->ranges_value = NULL;
			node->ranges_length = 0U;
			node->ranges_present = 0;
			node->cpus = arm64_fdt_name_is(
			    view.structure + name_start, offset - name_start,
			    "cpus");
			node->cpu =
			    depth > 0U && nodes[depth - 1U].cpus &&
			    arm64_fdt_name_has_prefix(
				view.structure + name_start, offset - name_start,
				"cpu@");
			node->psci = arm64_fdt_name_is(
			    view.structure + name_start, offset - name_start,
			    "psci");
			node->pl011 = 0;
			node->pl031 = 0;
			node->genet = 0;
			node->disabled = 0;
			node->interrupts_value = NULL;
			node->interrupts_length = 0U;
			node->mac_value = NULL;
			node->mac_length = 0U;
			node->phy_mode[0] = '\0';
			node->phy_addr = 0U;
			if (node->cpu)
			{
				info->cpu_count++;
			}
			depth++;
			offset = (offset + 4U) & ~3U;
		}
		else if (token == ARM64_FDT_END_NODE)
		{
			if (depth == 0U ||
			    finish_console(info, nodes, depth) != 0 ||
			    finish_rtc(info, nodes, depth) != 0 ||
			    finish_irq_controller(info, nodes, depth) != 0 ||
			    finish_genet(info, nodes, depth) != 0)
			{
				return -1;
			}
			depth--;
		}
		else if (token == ARM64_FDT_PROP)
		{
			struct arm64_fdt_device_node *node;
			const volatile uint8_t *value;
			uint32_t property_length;
			uint32_t name_offset;

			if (depth == 0U || offset > view.structure_size ||
			    view.structure_size - offset < 8U)
			{
				return -1;
			}
			property_length =
			    arm64_fdt_read_be32(view.structure + offset);
			name_offset =
			    arm64_fdt_read_be32(view.structure + offset + 4U);
			offset += 8U;
			if (property_length > view.structure_size - offset)
			{
				return -1;
			}
			value = view.structure + offset;
			node = &nodes[depth - 1U];
			if (property_length == 4U &&
			    arm64_fdt_prop_name_is(&view, name_offset,
						   "#address-cells"))
			{
				node->child_address_cells =
				    arm64_fdt_read_be32(value);
			}
			else if (property_length == 4U &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"#size-cells"))
			{
				node->child_size_cells =
				    arm64_fdt_read_be32(value);
			}
			else if (arm64_fdt_prop_name_is(
				     &view, name_offset, "compatible"))
			{
				note_hypervisor_compatible(info, value,
							   property_length);
				if (arm64_fdt_string_list_has(
					value, property_length, "arm,gic-v3"))
				{
					node->irq_model =
					    ARM64_IRQ_CONTROLLER_GIC_V3;
				}
				else if (arm64_fdt_string_list_has(
					     value, property_length,
					     "arm,gic-400") ||
					 arm64_fdt_string_list_has(
					     value, property_length,
					     "arm,cortex-a15-gic"))
				{
					node->irq_model =
					    ARM64_IRQ_CONTROLLER_GIC_V2;
				}
				else if (arm64_fdt_string_list_has(
					     value, property_length,
					     "brcm,bcm2836-l1-intc"))
				{
					node->irq_model =
					    ARM64_IRQ_CONTROLLER_BCM2836_LOCAL;
				}
				if (arm64_fdt_string_list_has(
					value, property_length, "arm,pl011"))
				{
					node->pl011 = 1;
				}
				if (arm64_fdt_string_list_has(
					value, property_length, "arm,pl031"))
				{
					node->pl031 = 1;
				}
				if (arm64_fdt_string_list_has(
					value, property_length,
					"brcm,bcm2711-genet-v5"))
				{
					node->genet = 1;
				}
				if (arm64_fdt_string_list_has(
					value, property_length,
					"arm,armv8-timer"))
				{
					info->architected_timer = 1;
				}
				if (arm64_fdt_string_list_has_prefix(
					value, property_length,
					"raspberrypi,rp1-"))
				{
					info->rp1_present = 1;
				}
			}
			else if (node->psci &&
				 arm64_fdt_prop_name_is(&view, name_offset,
							"method"))
			{
				if (arm64_fdt_string_list_has(
					value, property_length, "hvc"))
				{
					info->psci_conduit =
					    ARM64_PSCI_CONDUIT_HVC;
				}
				else if (arm64_fdt_string_list_has(
					     value, property_length, "smc"))
				{
					info->psci_conduit =
					    ARM64_PSCI_CONDUIT_SMC;
				}
			}
			else if (arm64_fdt_prop_name_is(&view, name_offset,
							 "status") &&
				 arm64_fdt_string_list_has(
				     value, property_length, "disabled"))
			{
				node->disabled = 1;
			}
			else if (arm64_fdt_prop_name_is(&view, name_offset,
							 "reg"))
			{
				uint32_t ancestor;

				node->reg_value = value;
				node->reg_length = property_length;
				/*
				 * ethernet-phy@N uses a 4-byte unit address.
				 * Promote it to the enclosing GENET node.
				 */
				if (property_length == 4U)
				{
					for (ancestor = 1U; ancestor < depth;
					     ancestor++)
					{
						if (nodes[depth - 1U - ancestor]
							    .genet)
						{
							nodes[depth - 1U -
							      ancestor]
								.phy_addr =
							    arm64_fdt_read_be32(
								    value);
							break;
						}
					}
				}
			}
			else if (arm64_fdt_prop_name_is(&view, name_offset,
							 "ranges"))
			{
				node->ranges_value = value;
				node->ranges_length = property_length;
				node->ranges_present = 1;
			}
			else if (arm64_fdt_prop_name_is(&view, name_offset,
							 "interrupts"))
			{
				node->interrupts_value = value;
				node->interrupts_length = property_length;
			}
			else if (arm64_fdt_prop_name_is(&view, name_offset,
							 "local-mac-address") ||
				 arm64_fdt_prop_name_is(&view, name_offset,
							 "mac-address"))
			{
				node->mac_value = value;
				node->mac_length = property_length;
			}
			else if (arm64_fdt_prop_name_is(&view, name_offset,
							 "phy-mode"))
			{
				uint32_t n = property_length < 15U ?
						     property_length : 15U;
				uint32_t i;

				for (i = 0; i < n && value[i] != 0U; i++)
					node->phy_mode[i] = (char)value[i];
				node->phy_mode[i] = '\0';
			}
			if (property_length > UINT32_MAX - offset - 3U)
			{
				return -1;
			}
			offset = (offset + property_length + 3U) & ~3U;
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
