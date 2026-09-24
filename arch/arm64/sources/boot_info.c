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

#define FDT_MAGIC        0xd00dfeedU
#define FDT_HEADER_SIZE  40U
#define FDT_MAX_SIZE     (2U * 1024U * 1024U)

/* Written by the stackless entry assembly before x0 is reused. */
uintptr_t arm64_firmware_fdt;

static struct arm64_board_boot_info g_boot_info;

static uint32_t read_be32(const volatile uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int arm64_fdt_boot_info_init(uintptr_t fdt_pa)
{
	const volatile uint8_t *header;
	uint32_t size;

	g_boot_info.fdt_pa = fdt_pa;
	g_boot_info.fdt_size = 0;
	g_boot_info.fdt_valid = 0;

	/* Linux arm64 boot protocol requires an 8-byte-aligned DTB in RAM. */
	if (!fdt_pa || (fdt_pa & 7U) != 0)
	{
		return -1;
	}

	header = (const volatile uint8_t *)fdt_pa;
	if (read_be32(header) != FDT_MAGIC)
	{
		return -1;
	}
	size = read_be32(header + 4);
	if (size < FDT_HEADER_SIZE || size > FDT_MAX_SIZE)
	{
		return -1;
	}

	g_boot_info.fdt_size = size;
	g_boot_info.fdt_valid = 1;
	return 0;
}

const struct arm64_board_boot_info *arm64_board_boot_info(void)
{
	return &g_boot_info;
}
