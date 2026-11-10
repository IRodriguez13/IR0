/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: swap_io.c
 * Description: 64-bit LBA swap page I/O through the block facade.
 */

#include "swap_internal.h"
#include <ir0/errno.h>
#include <ir0/swap.h>

#define SWAP_SECTORS_PER_PAGE 8U

int ir0_swap_read_page(unsigned type, uint64_t offset, void *buf)
{
	struct ir0_swap_area *area;
	uint64_t lba;
	int rc;

	if (!buf)
		return -EINVAL;
	area = ir0_swap_area_at(type);
	if (!area)
		return -ENODEV;
	rc = ir0_swap_map_lba(area, offset, &lba);
	if (rc < 0)
		return rc;
	return ir0_block_read(area->dev, lba, SWAP_SECTORS_PER_PAGE, buf);
}

int ir0_swap_write_page(unsigned type, uint64_t offset, const void *buf)
{
	struct ir0_swap_area *area;
	uint64_t lba;
	int rc;

	if (!buf)
		return -EINVAL;
	area = ir0_swap_area_at(type);
	if (!area)
		return -ENODEV;
	rc = ir0_swap_map_lba(area, offset, &lba);
	if (rc < 0)
		return rc;
	return ir0_block_write(area->dev, lba, SWAP_SECTORS_PER_PAGE, buf);
}
