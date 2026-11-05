/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_mm_port_mock.c
 * Description: KTM MM stats facade mock — deterministic /proc numbers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/mm_port.h>
#include <ktm_mock_mm_port.h>

static size_t g_pmm_total = 1024;
static size_t g_pmm_used = 256;
static size_t g_pmm_free = 768;
static size_t g_alloc_total = 4096;
static size_t g_alloc_used = 1024;
static size_t g_alloc_allocs = 7;
static uintptr_t g_range_start = 0x100000u;
static uintptr_t g_range_end = 0x4000000u;
static int g_pmm_stats_calls;
static int g_alloc_stats_calls;

void ktm_mm_port_mock_reset(void)
{
	g_pmm_total = 1024;
	g_pmm_used = 256;
	g_pmm_free = 768;
	g_alloc_total = 4096;
	g_alloc_used = 1024;
	g_alloc_allocs = 7;
	g_range_start = 0x100000u;
	g_range_end = 0x4000000u;
	g_pmm_stats_calls = 0;
	g_alloc_stats_calls = 0;
}

void ktm_mm_port_mock_set_pmm_stats(size_t total_frames, size_t used_frames,
				    size_t free_frames)
{
	g_pmm_total = total_frames;
	g_pmm_used = used_frames;
	g_pmm_free = free_frames;
}

void ktm_mm_port_mock_set_alloc_stats(size_t total, size_t used, size_t allocs)
{
	g_alloc_total = total;
	g_alloc_used = used;
	g_alloc_allocs = allocs;
}

void ktm_mm_port_mock_set_range(uintptr_t start, uintptr_t end)
{
	g_range_start = start;
	g_range_end = end;
}

void ir0_mm_pmm_stats(size_t *total_frames, size_t *used_frames,
		      size_t *free_frames)
{
	g_pmm_stats_calls++;
	if (total_frames)
		*total_frames = g_pmm_total;
	if (used_frames)
		*used_frames = g_pmm_used;
	if (free_frames)
		*free_frames = g_pmm_free;
}

void ir0_mm_alloc_stats(size_t *total, size_t *used, size_t *allocs)
{
	g_alloc_stats_calls++;
	if (total)
		*total = g_alloc_total;
	if (used)
		*used = g_alloc_used;
	if (allocs)
		*allocs = g_alloc_allocs;
}

uintptr_t ir0_mm_pmm_start(void)
{
	return g_range_start;
}

uintptr_t ir0_mm_pmm_end(void)
{
	return g_range_end;
}

int ktm_mm_port_mock_pmm_stats_calls(void)
{
	return g_pmm_stats_calls;
}

int ktm_mm_port_mock_alloc_stats_calls(void)
{
	return g_alloc_stats_calls;
}
