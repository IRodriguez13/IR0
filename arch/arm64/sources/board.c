/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: board.c
 * Description: ARM64 board descriptors — qemu-virt, rpi4, rpi5 stub.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arm64_board.h>
#include <ir0/boot_log.h>

#include <stddef.h>

#if defined(IR0_ARM64_BOARD_RPI5)
static const struct arm64_board_desc g_board = {
	.name = "rpi5",
	.uart = "none",
	.uart_mmio = 0,
	.arch_line = "isa=arm64 board=rpi5 uart=none",
	.uart_mmio_line = NULL,
	.platform_ops = &arm64_rpi_platform_ops,
	.boot_info_init = arm64_fdt_boot_info_init,
};
#elif defined(IR0_ARM64_BOARD_RPI4)
static const struct arm64_board_desc g_board = {
	.name = "rpi4",
	.uart = "pl011",
	.uart_mmio = 0xfe201000UL,
	.arch_line = "isa=arm64 board=rpi4 uart=pl011",
	.uart_mmio_line = "uart_mmio=0xfe201000",
	.platform_ops = &arm64_rpi_platform_ops,
	.boot_info_init = arm64_fdt_boot_info_init,
};
#else
/* Default: QEMU virt PL011 @ 0x09000000 */
static const struct arm64_board_desc g_board = {
	.name = "qemu-virt",
	.uart = "pl011",
	.uart_mmio = 0x09000000UL,
	.arch_line = "isa=arm64 board=qemu-virt uart=pl011",
	.uart_mmio_line = "uart_mmio=0x09000000",
	.platform_ops = &arm64_virt_platform_ops,
	.boot_info_init = arm64_fdt_boot_info_init,
};
#endif

const struct arm64_board_desc *arm64_board_get(void)
{
	return &g_board;
}

int arm64_board_capture_boot_info(uintptr_t fdt_pa)
{
	const struct arm64_board_desc *b = arm64_board_get();

	if (!b || !b->boot_info_init)
	{
		return -1;
	}
	return b->boot_info_init(fdt_pa);
}

void arm64_board_apply_platform(void)
{
	const struct arm64_board_desc *b = arm64_board_get();

	if (b && b->platform_ops)
	{
		ir0_platform_ops_set(b->platform_ops);
	}
}

void arm64_board_log_arch(void)
{
	const struct arm64_board_desc *b = arm64_board_get();
	const struct arm64_board_boot_info *boot;

	if (!b)
	{
		return;
	}
	boot = arm64_board_boot_info();

	ir0_boot_arch(b->arch_line);
	if (b->uart_mmio_line)
	{
		ir0_boot_arch(b->uart_mmio_line);
	}
	if (b->uart_mmio == 0)
	{
		ir0_boot_warn("ARCH", "uart=none board=rpi5");
		ir0_boot_smoke("ARM64_BOARD_RPI5_STUB");
	}
	if (boot->fdt_valid)
	{
		ir0_boot_arch("dtb=firmware-x0");
		ir0_boot_smoke("ARM64_DTB_OK");
		if (boot->memory_range_count > 0)
		{
			ir0_boot_info_hex64("ARCH", "dtb_ram_base",
					    boot->memory[0].base);
			ir0_boot_info_hex64("ARCH", "dtb_ram_size",
					    boot->memory[0].size);
			ir0_boot_smoke("ARM64_DTB_MEMORY_OK");
		}
		else
		{
			ir0_boot_warn("ARCH", "DTB has no usable memory range");
			ir0_boot_smoke("ARM64_DTB_MEMORY_FAIL");
		}
		ir0_boot_info_hex64("ARCH", "dtb_cpu_count", boot->cpu_count);
		if (boot->cpu_count > 0U)
			ir0_boot_smoke("ARM64_DTB_CPU_OK");
		else
			ir0_boot_smoke("ARM64_DTB_CPU_FAIL");
		ir0_boot_info_hex64("ARCH", "dtb_reserved_count",
				    boot->reserved_range_count);
		ir0_boot_info_hex64("ARCH", "dtb_usable_count",
				    boot->usable_range_count);
		if (boot->usable_range_count > 0U)
		{
			ir0_boot_info_hex64("ARCH", "usable_ram_base",
					    boot->usable[0].base);
			ir0_boot_info_hex64("ARCH", "usable_ram_size",
					    boot->usable[0].size);
			ir0_boot_smoke("ARM64_DTB_USABLE_MEMORY_OK");
		}
		else
			ir0_boot_smoke("ARM64_DTB_USABLE_MEMORY_FAIL");
	}
	else
	{
		ir0_boot_warn("ARCH", "firmware DTB missing or invalid");
		ir0_boot_info_hex64("ARCH", "dtb_x0", (uint64_t)boot->fdt_pa);
		ir0_boot_info_hex64("ARCH", "dtb_magic", boot->fdt_magic);
		ir0_boot_info_hex64("ARCH", "dtb_size", boot->fdt_size);
		ir0_boot_smoke("ARM64_DTB_FAIL");
	}
}
