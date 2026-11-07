/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: boot_info_private.h
 * Description: ARM64-private firmware boot information parser interfaces.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <arm64_board.h>

#include <stddef.h>
#include <stdint.h>

#define ARM64_FDT_MAGIC 0xd00dfeedU
#define ARM64_FDT_HEADER_SIZE 40U
#define ARM64_FDT_MAX_SIZE (2U * 1024U * 1024U)
#define ARM64_FDT_BEGIN_NODE 1U
#define ARM64_FDT_END_NODE 2U
#define ARM64_FDT_PROP 3U
#define ARM64_FDT_NOP 4U
#define ARM64_FDT_END 9U
#define ARM64_FDT_NODE_DEPTH_MAX 32U

struct arm64_fdt_view
{
	const volatile uint8_t *blob;
	uint32_t total_size;
	const volatile uint8_t *structure;
	uint32_t structure_size;
	const volatile uint8_t *strings;
	uint32_t strings_size;
};

struct arm64_boot_parse_context
{
	struct arm64_board_boot_info *info;
};

uint32_t arm64_fdt_read_be32(const volatile uint8_t *p);
uint64_t arm64_fdt_read_cells(const volatile uint8_t *p, uint32_t cells);
int arm64_fdt_view_init(struct arm64_fdt_view *view,
			const volatile uint8_t *fdt, uint32_t total);
int arm64_fdt_name_is(const volatile uint8_t *name, uint32_t available,
		      const char *wanted);
int arm64_fdt_name_has_prefix(const volatile uint8_t *name,
			      uint32_t available, const char *prefix);
int arm64_fdt_prop_name_is(const struct arm64_fdt_view *view,
			   uint32_t name_offset, const char *wanted);
int arm64_fdt_string_list_has(const volatile uint8_t *value, uint32_t length,
			      const char *wanted);
int arm64_fdt_string_list_has_prefix(const volatile uint8_t *value,
				     uint32_t length, const char *prefix);
int arm64_boot_append_range(struct ir0_phys_range *ranges, uint32_t *count,
			    uint32_t maximum, uint64_t base, uint64_t size);

int arm64_boot_parse_reservations(struct arm64_boot_parse_context *context,
				  const volatile uint8_t *fdt,
				  uint32_t total);
int arm64_boot_parse_platform(struct arm64_boot_parse_context *context,
			      const volatile uint8_t *fdt, uint32_t total);
int arm64_boot_parse_devices(struct arm64_boot_parse_context *context,
			     const volatile uint8_t *fdt, uint32_t total);
