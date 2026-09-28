/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: all_objs_mark.c
 * Description: Marker TU forced into kernel-arm64-all.bin link.
 */

#include "pl011.h"
#include <ir0/arch_mm.h>
#include <ir0/boot_log.h>

static int arm64_mm_descriptor_contract(void)
{
	uint64_t leaf;
	uint64_t flags;

	leaf = mm_make_leaf_pte(0x12345000UL,
				IR0_MM_MAP_USER | IR0_MM_MAP_WRITE, 1);
	flags = mm_pte_mapping_flags(leaf);
	if (mm_pte_phys(leaf) != 0x12345000UL ||
	    (flags & (IR0_MM_MAP_PRESENT | IR0_MM_MAP_USER |
		      IR0_MM_MAP_WRITE | IR0_MM_MAP_EXEC)) !=
		(IR0_MM_MAP_PRESENT | IR0_MM_MAP_USER |
		 IR0_MM_MAP_WRITE | IR0_MM_MAP_EXEC))
		return -1;

	mm_pte_mark_cow(&leaf);
	flags = mm_pte_mapping_flags(leaf);
	if ((flags & IR0_MM_MAP_WRITE) || !(flags & IR0_MM_MAP_COW) ||
	    !(flags & IR0_MM_MAP_USER))
		return -1;

	leaf = mm_make_leaf_pte(0x12346000UL, IR0_MM_MAP_USER, 0);
	if (mm_pte_executable(leaf) ||
	    (mm_pte_mapping_flags(leaf) & IR0_MM_MAP_EXEC))
		return -1;

	return 0;
}

void arm64_all_objs_mark(void)
{
	ir0_boot_smoke("ARM64_ALL_OBJS_LINK_OK");
	if (arm64_mm_descriptor_contract() == 0)
		ir0_boot_smoke("ARM64_MM_DESCRIPTOR_CONTRACT_OK");
	else
		ir0_boot_smoke("ARM64_MM_DESCRIPTOR_CONTRACT_FAIL");
}
