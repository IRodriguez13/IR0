/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: page_table_pool.h
 * Description: Portable PMM-backed ownership for translation-table frames.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/mm_alloc.h>
#include <stdint.h>

uintptr_t page_table_frame_alloc(mm_alloc_context_t context);
void page_table_frame_free(uintptr_t frame);
