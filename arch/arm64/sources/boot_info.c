/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_info.c
 * Description: ARM64 firmware boot information coordinator.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "boot_info_private.h"

#include <config.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARM64_FRAME_SIZE 4096U

/*
 * Written by stackless entry (entry.S) before kmain; must be volatile so the C
 * compiler does not assume it stays zero across the asm handoff.
 */
volatile uintptr_t arm64_firmware_fdt;

static struct arm64_board_boot_info g_boot_info;
static int g_memory_finalized;
#if defined(IR0_ARM64_BOARD_QEMU_VIRT)
static int g_qemu_virt_synthetic_ram;
#endif

static uint64_t min_u64(uint64_t a, uint64_t b)
{
	return a < b ? a : b;
}

static uint64_t max_u64(uint64_t a, uint64_t b)
{
	return a > b ? a : b;
}

static int ranges_overlap(const struct ir0_phys_range *a,
			  const struct ir0_phys_range *b)
{
	return a->base < b->base + b->size &&
	       b->base < a->base + a->size;
}

static int append_usable(uint64_t base, uint64_t end)
{
	struct ir0_phys_range *range;

	if (end <= base)
	{
		return 0;
	}
	if (g_boot_info.usable_range_count >= ARM64_BOOT_USABLE_RANGES_MAX)
	{
		return -1;
	}
	range = &g_boot_info.usable[g_boot_info.usable_range_count++];
	range->base = base;
	range->size = end - base;
	return 0;
}

static void reset_boot_info(uintptr_t fdt_pa)
{
	memset(&g_boot_info, 0, sizeof(g_boot_info));
	g_boot_info.fdt_pa = fdt_pa;
	g_boot_info.irq_controller = ARM64_IRQ_CONTROLLER_UNKNOWN;
	g_boot_info.psci_conduit = ARM64_PSCI_CONDUIT_UNKNOWN;
	g_boot_info.console_uart = ARM64_UART_UNKNOWN;
	g_boot_info.hypervisor_kind = ARM64_HV_NONE;
	g_memory_finalized = 0;
#if defined(IR0_ARM64_BOARD_QEMU_VIRT)
	g_qemu_virt_synthetic_ram = 0;
#endif
}

#if defined(IR0_ARM64_BOARD_QEMU_VIRT)
/*
 * QEMU virt can leave the DTB address unreadable to EL1 before the MMU maps
 * RAM. Preserve the existing bounded 256 MiB fallback used by product smokes.
 */
static int append_qemu_virt_synthetic_ram(void)
{
	if (g_boot_info.memory_range_count != 0U)
		return 0;
	if (arm64_boot_append_range(
		g_boot_info.memory, &g_boot_info.memory_range_count,
		ARM64_BOOT_MEMORY_RANGES_MAX, 0x40000000UL,
		0x10000000UL) != 0)
		return -1;
	g_qemu_virt_synthetic_ram = 1;
	return 0;
}

static void install_qemu_virt_synthetic_ram(void)
{
	reset_boot_info(0U);
	(void)append_qemu_virt_synthetic_ram();
}
#endif

static int build_usable_ranges(void)
{
	uint32_t memory_index;

	g_boot_info.usable_range_count = 0U;
	g_memory_finalized = 0;
	for (memory_index = 0U;
	     memory_index < g_boot_info.memory_range_count; memory_index++)
	{
		struct ir0_phys_range current[ARM64_BOOT_USABLE_RANGES_MAX];
		struct ir0_phys_range next[ARM64_BOOT_USABLE_RANGES_MAX];
		uint32_t current_count = 1U;
		uint32_t reserved_index;
		uint64_t memory_start =
		    g_boot_info.memory[memory_index].base;
		uint64_t memory_end =
		    memory_start + g_boot_info.memory[memory_index].size;

		if (memory_start > UINT64_MAX - (ARM64_FRAME_SIZE - 1U))
		{
			continue;
		}
		memory_start = (memory_start + ARM64_FRAME_SIZE - 1U) &
			       ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
		memory_end &= ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
		if (memory_end <= memory_start)
		{
			continue;
		}
		current[0].base = memory_start;
		current[0].size = memory_end - memory_start;

		for (reserved_index = 0U;
		     reserved_index < g_boot_info.reserved_range_count;
		     reserved_index++)
		{
			uint32_t i;
			uint32_t next_count = 0U;
			uint64_t reserved_start =
			    g_boot_info.reserved[reserved_index].base;
			uint64_t reserved_end = reserved_start +
			    g_boot_info.reserved[reserved_index].size;

			reserved_start &=
			    ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
			if ((reserved_end & (ARM64_FRAME_SIZE - 1U)) != 0U)
			{
				if (reserved_end >
				    UINT64_MAX - (ARM64_FRAME_SIZE - 1U))
				{
					reserved_end = UINT64_MAX;
				}
				else
				{
					reserved_end =
					    (reserved_end +
					     ARM64_FRAME_SIZE - 1U) &
					    ~(uint64_t)(ARM64_FRAME_SIZE - 1U);
				}
			}
			for (i = 0U; i < current_count; i++)
			{
				uint64_t start = current[i].base;
				uint64_t end = start + current[i].size;
				uint64_t cut_start =
				    max_u64(start, reserved_start);
				uint64_t cut_end =
				    min_u64(end, reserved_end);

				if (cut_start >= cut_end)
				{
					if (next_count >=
					    ARM64_BOOT_USABLE_RANGES_MAX)
					{
						return -1;
					}
					next[next_count++] = current[i];
					continue;
				}
				if (start < cut_start)
				{
					if (next_count >=
					    ARM64_BOOT_USABLE_RANGES_MAX)
					{
						return -1;
					}
					next[next_count].base = start;
					next[next_count++].size =
					    cut_start - start;
				}
				if (cut_end < end)
				{
					if (next_count >=
					    ARM64_BOOT_USABLE_RANGES_MAX)
					{
						return -1;
					}
					next[next_count].base = cut_end;
					next[next_count++].size =
					    end - cut_end;
				}
			}
			current_count = next_count;
			for (i = 0U; i < current_count; i++)
			{
				current[i] = next[i];
			}
		}
		for (reserved_index = 0U; reserved_index < current_count;
		     reserved_index++)
		{
			if (append_usable(
				current[reserved_index].base,
				current[reserved_index].base +
				    current[reserved_index].size) != 0)
			{
				return -1;
			}
		}
	}
	return 0;
}

int arm64_fdt_boot_info_init(uintptr_t fdt_pa)
{
	struct arm64_boot_parse_context context;
	const volatile uint8_t *header;
	uint32_t size;

	reset_boot_info(fdt_pa);
	if (fdt_pa == 0U || (fdt_pa & 7U) != 0U ||
	    fdt_pa > UINTPTR_MAX - ARM64_FDT_HEADER_SIZE)
	{
		return -1;
	}
	header = (const volatile uint8_t *)fdt_pa;
	g_boot_info.fdt_magic = arm64_fdt_read_be32(header);
	if (g_boot_info.fdt_magic != ARM64_FDT_MAGIC)
	{
		return -1;
	}
	size = arm64_fdt_read_be32(header + 4U);
	g_boot_info.fdt_size = size;
	if (size < ARM64_FDT_HEADER_SIZE || size > ARM64_FDT_MAX_SIZE ||
	    fdt_pa > UINTPTR_MAX - size)
	{
		return -1;
	}
	context.info = &g_boot_info;
	if (arm64_boot_parse_reservations(&context, header, size) != 0 ||
	    arm64_boot_parse_platform(&context, header, size) != 0 ||
	    arm64_boot_parse_devices(&context, header, size) != 0)
	{
		return -1;
	}
	g_boot_info.fdt_valid = 1;
	return 0;
}

int arm64_fdt_boot_info_init_fallback(uintptr_t primary, uintptr_t fallback)
{
	if (arm64_fdt_boot_info_init(primary) == 0)
	{
		return 0;
	}
	if (fallback != 0U && fallback != primary &&
	    arm64_fdt_boot_info_init(fallback) == 0)
	{
		return 0;
	}
#if defined(IR0_ARM64_BOARD_QEMU_VIRT)
#if CONFIG_ARM64_SYNTHETIC_RAM_FALLBACK
	install_qemu_virt_synthetic_ram();
	return g_qemu_virt_synthetic_ram ? 0 : -1;
#else
	return -1;
#endif
#else
	(void)fallback;
	return -1;
#endif
}

int arm64_board_hypervisor_present(void)
{
	return g_boot_info.fdt_valid &&
	       g_boot_info.hypervisor_kind != ARM64_HV_NONE;
}

int arm64_board_hypervisor_vendor(char *buf, size_t n)
{
	if (buf == NULL || n < 13U)
	{
		return -1;
	}
	if (!arm64_board_hypervisor_present())
	{
		buf[0] = '\0';
		return -1;
	}
	strncpy(buf, g_boot_info.hypervisor_vendor, n);
	buf[n - 1U] = '\0';
	return 0;
}

const struct arm64_board_boot_info *arm64_board_boot_info(void)
{
	return &g_boot_info;
}

int arm64_board_finalize_memory(uintptr_t kernel_base, size_t kernel_size)
{
	struct ir0_phys_range kernel;
	uint32_t extra_reservations;
	uint32_t saved_reserved;
	uint32_t i;
	int have_ram_bank;

	have_ram_bank =
	    g_boot_info.fdt_valid ||
#if defined(IR0_ARM64_BOARD_QEMU_VIRT)
	    g_qemu_virt_synthetic_ram;
#else
	    0;
#endif
	if (!have_ram_bank || kernel_size == 0U ||
	    (uint64_t)kernel_base >
		UINT64_MAX - (uint64_t)kernel_size)
	{
		return -1;
	}
	if (g_memory_finalized)
	{
		return 0;
	}
	if ((g_boot_info.initrd_start == 0U) !=
		(g_boot_info.initrd_end == 0U) ||
	    (g_boot_info.initrd_start != 0U &&
	     g_boot_info.initrd_end <= g_boot_info.initrd_start))
	{
		g_boot_info.initrd_start = 0U;
		g_boot_info.initrd_end = 0U;
	}
	extra_reservations = 1U;
	if (g_boot_info.fdt_valid)
	{
		extra_reservations++;
	}
	if (g_boot_info.initrd_start != 0U)
	{
		extra_reservations++;
	}
	if (g_boot_info.framebuffer.size != 0U)
	{
		extra_reservations++;
	}
	if (g_boot_info.reserved_range_count >
	    ARM64_BOOT_RESERVED_RANGES_MAX - extra_reservations)
	{
		return -1;
	}
	kernel.base = (uint64_t)kernel_base;
	kernel.size = (uint64_t)kernel_size;
	for (i = 0U; i < g_boot_info.memory_range_count; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < g_boot_info.memory_range_count; j++)
		{
			if (ranges_overlap(&g_boot_info.memory[i],
					   &g_boot_info.memory[j]))
			{
				return -1;
			}
		}
	}
	saved_reserved = g_boot_info.reserved_range_count;
	if (g_boot_info.fdt_valid && g_boot_info.fdt_size != 0U &&
	    arm64_boot_append_range(
		g_boot_info.reserved, &g_boot_info.reserved_range_count,
		ARM64_BOOT_RESERVED_RANGES_MAX, g_boot_info.fdt_pa,
		g_boot_info.fdt_size) != 0)
	{
		g_boot_info.reserved_range_count = saved_reserved;
		return -1;
	}
	if (g_boot_info.initrd_start != 0U &&
	    arm64_boot_append_range(
		g_boot_info.reserved, &g_boot_info.reserved_range_count,
		ARM64_BOOT_RESERVED_RANGES_MAX, g_boot_info.initrd_start,
		g_boot_info.initrd_end - g_boot_info.initrd_start) != 0)
	{
		g_boot_info.reserved_range_count = saved_reserved;
		return -1;
	}
	if (g_boot_info.framebuffer.size != 0U &&
	    arm64_boot_append_range(
		g_boot_info.reserved, &g_boot_info.reserved_range_count,
		ARM64_BOOT_RESERVED_RANGES_MAX, g_boot_info.framebuffer.base,
		g_boot_info.framebuffer.size) != 0)
	{
		g_boot_info.reserved_range_count = saved_reserved;
		return -1;
	}
	if (arm64_boot_append_range(
		g_boot_info.reserved, &g_boot_info.reserved_range_count,
		ARM64_BOOT_RESERVED_RANGES_MAX, kernel.base,
		kernel.size) != 0 ||
	    build_usable_ranges() != 0 ||
	    g_boot_info.usable_range_count == 0U)
	{
		g_boot_info.reserved_range_count = saved_reserved;
		g_boot_info.usable_range_count = 0U;
		return -1;
	}
	g_memory_finalized = 1;
	return 0;
}

int arm64_board_prepare_memory(uintptr_t kernel_base, size_t kernel_size)
{
#if defined(IR0_ARM64_BOARD_QEMU_VIRT)
#if CONFIG_ARM64_SYNTHETIC_RAM_FALLBACK
	(void)append_qemu_virt_synthetic_ram();
#endif
#endif
	if (arm64_board_finalize_memory(kernel_base, kernel_size) == 0)
		return 0;
#if defined(IR0_ARM64_BOARD_QEMU_VIRT)
#if CONFIG_ARM64_SYNTHETIC_RAM_FALLBACK
	g_memory_finalized = 0;
	g_boot_info.memory_range_count = 0U;
	g_boot_info.usable_range_count = 0U;
	if (append_qemu_virt_synthetic_ram() == 0)
		return arm64_board_finalize_memory(kernel_base, kernel_size);
#endif
#endif
	return -1;
}
