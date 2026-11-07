/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: swap_internal.h
 * Description: Private swap-area table used by mm/swap modules.
 */

#pragma once

#include <ir0/swap.h>
#include <ir0/blockdev.h>

struct ir0_swap_area
{
	int used;
	int kind;
	int prio;
	char path[256];
	dev_t dev;
	uint32_t last_page;
	uint32_t inuse;
	uint8_t *bitmap;
	uint16_t *refs;
	uint8_t *state;
	struct ir0_swap_extent extents[IR0_SWAP_MAX_EXTENTS];
	size_t nextent;
};

struct ir0_swap_area *ir0_swap_area_at(unsigned type);
unsigned ir0_swap_area_count(void);
int ir0_swap_map_lba(const struct ir0_swap_area *area, uint64_t page,
		     uint64_t *lba_out);
