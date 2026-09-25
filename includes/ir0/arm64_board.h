/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arm64_board.h
 * Description: Compile-time ARM64 board descriptor (UART MMIO, platform_ops).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/platform_ops.h>
#include <ir0/platform_resource.h>
#include <stddef.h>
#include <stdint.h>

#define ARM64_BOOT_MEMORY_RANGES_MAX 8U
#define ARM64_BOOT_RESERVED_RANGES_MAX 16U
#define ARM64_BOOT_USABLE_RANGES_MAX 32U

enum arm64_irq_controller_model
{
	ARM64_IRQ_CONTROLLER_UNKNOWN = 0,
	ARM64_IRQ_CONTROLLER_GIC_V2,
	ARM64_IRQ_CONTROLLER_GIC_V3,
};

enum arm64_psci_conduit
{
	ARM64_PSCI_CONDUIT_UNKNOWN = 0,
	ARM64_PSCI_CONDUIT_HVC,
	ARM64_PSCI_CONDUIT_SMC,
};

struct arm64_board_desc
{
	const char *name;	 /* qemu-virt | rpi4 | rpi5 */
	const char *uart;	 /* pl011 | none */
	uintptr_t uart_mmio; /* 0 = no UART (honest stub) */
	const char *arch_line;	  /* for ir0_boot_arch() */
	const char *uart_mmio_line; /* optional second ARCH line; may be NULL */
	const struct ir0_platform_ops *platform_ops;
	int (*boot_info_init)(uintptr_t fdt_pa);
};

struct arm64_board_boot_info
{
	uintptr_t fdt_pa;
	uint32_t fdt_size;
	uint32_t fdt_magic;
	int fdt_valid;
	uint32_t cpu_count;
	enum arm64_irq_controller_model irq_controller;
	enum arm64_psci_conduit psci_conduit;
	int architected_timer;
	int rp1_present;
	uint32_t memory_range_count;
	struct ir0_phys_range memory[ARM64_BOOT_MEMORY_RANGES_MAX];
	uint32_t reserved_range_count;
	struct ir0_phys_range reserved[ARM64_BOOT_RESERVED_RANGES_MAX];
	uint32_t usable_range_count;
	struct ir0_phys_range usable[ARM64_BOOT_USABLE_RANGES_MAX];
};

extern const struct ir0_platform_ops arm64_virt_platform_ops;
extern const struct ir0_platform_ops arm64_rpi_platform_ops;

const struct arm64_board_desc *arm64_board_get(void);

/* Select platform_ops from the active board (call once early). */
void arm64_board_apply_platform(void);

/* Capture firmware x0 through the board-selected boot information backend. */
int arm64_board_capture_boot_info(uintptr_t fdt_pa);
const struct arm64_board_boot_info *arm64_board_boot_info(void);

/* Shared ARM64 FDT backend used by DT-described boards. */
int arm64_fdt_boot_info_init(uintptr_t fdt_pa);

/* Subtract kernel, DTB and firmware reservations from discovered RAM. */
int arm64_board_finalize_memory(uintptr_t kernel_base, size_t kernel_size);

/* Emit ARCH lines (and WARN if uart_mmio == 0). */
void arm64_board_log_arch(void);
