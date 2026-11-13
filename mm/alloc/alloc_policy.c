/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: alloc_policy.c
 * Description: Bounded portable frame allocation and direct-reclaim policy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/mm_alloc.h>
#include <ir0/pmm.h>
#include <ir0/reclaim.h>

#define MM_DIRECT_RECLAIM_ROUNDS 2U

uintptr_t mm_alloc_frame(mm_alloc_context_t context)
{
	uintptr_t frame;
	unsigned int round;

	frame = pmm_alloc_frame();
	if (frame != 0 || context != MM_ALLOC_DIRECT_RECLAIM)
		return frame;

	for (round = 0; round < MM_DIRECT_RECLAIM_ROUNDS; round++)
	{
		struct ir0_reclaim_result result;

		if (mm_reclaim_run(1, &result) != 0 ||
		    result.reclaimed_pages == 0)
			break;
		frame = pmm_alloc_frame();
		if (frame != 0)
			return frame;
	}
	return 0;
}
