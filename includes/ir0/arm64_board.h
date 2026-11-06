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
	ARM64_IRQ_CONTROLLER_BCM2836_LOCAL,
};

enum arm64_psci_conduit
{
	ARM64_PSCI_CONDUIT_UNKNOWN = 0,
	ARM64_PSCI_CONDUIT_HVC,
	ARM64_PSCI_CONDUIT_SMC,
};

enum arm64_uart_model
{
	ARM64_UART_UNKNOWN = 0,
	ARM64_UART_PL011,
};

/* Best-effort from firmware DTB (see boot_info FDT parse). */
enum arm64_hypervisor_kind
{
	ARM64_HV_NONE = 0,
	ARM64_HV_QEMU,
	ARM64_HV_KVM,
	ARM64_HV_XEN,
	ARM64_HV_HYPERV,
	ARM64_HV_VMWARE,
	ARM64_HV_OTHER
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
	/*
	 * Physical address of a DTB when firmware left x0 empty. QEMU 8
	 * places that blob at the RAM base for an ELF -kernel and does not
	 * set x0 (hw/arm/boot.c: ELF images are not Linux). 0 disables it.
	 */
	uintptr_t dtb_fallback_pa;
};

struct arm64_board_boot_info
{
	uintptr_t fdt_pa;
	uint32_t fdt_size;
	uint32_t fdt_magic;
	int fdt_valid;
	uint32_t cpu_count;
	enum arm64_irq_controller_model irq_controller;
	uint32_t irq_range_count;
	struct ir0_phys_range irq_mmio[2];
	enum arm64_psci_conduit psci_conduit;
	int architected_timer;
	int rp1_present;
	enum arm64_uart_model console_uart;
	struct ir0_phys_range console_mmio;
	uint32_t memory_range_count;
	struct ir0_phys_range memory[ARM64_BOOT_MEMORY_RANGES_MAX];
	uint32_t reserved_range_count;
	struct ir0_phys_range reserved[ARM64_BOOT_RESERVED_RANGES_MAX];
	uint32_t usable_range_count;
	struct ir0_phys_range usable[ARM64_BOOT_USABLE_RANGES_MAX];
	enum arm64_hypervisor_kind hypervisor_kind;
	char hypervisor_vendor[16];
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

/*
 * Parse @primary. When that is not a DTB, parse @fallback unless it is 0
 * or the same address. A failing primary is not kept.
 */
int arm64_fdt_boot_info_init_fallback(uintptr_t primary, uintptr_t fallback);

/* Subtract kernel, DTB and firmware reservations from discovered RAM. */
int arm64_board_finalize_memory(uintptr_t kernel_base, size_t kernel_size);

/* Emit ARCH lines (and WARN if uart_mmio == 0). */
void arm64_board_log_arch(void);

/* Populated when FDT parsing sees hypervisor / guest compatible strings. */
int arm64_board_hypervisor_present(void);
int arm64_board_hypervisor_vendor(char *buf, size_t n);
