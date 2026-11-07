/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_alloc.h
 * Description: Portable physical-frame allocation policy.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef enum
{
	MM_ALLOC_ATOMIC = 0,
	MM_ALLOC_DIRECT_RECLAIM,
} mm_alloc_context_t;

/*
 * Atomic allocation never invokes reclaim. Direct reclaim is bounded and
 * allocation-free; callers still own the final OOM policy.
 */
uintptr_t mm_alloc_frame(mm_alloc_context_t context);
