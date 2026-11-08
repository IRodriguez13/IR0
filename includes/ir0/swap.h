/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: swap.h
 * Description: Portable swap areas, slots, and page I/O (Linux SWAPSPACE2).
 *
 * References:
 *   https://docs.kernel.org/mm/swap.html
 *   https://man7.org/linux/man-pages/man2/swapon.2.html
 *   include/uapi/linux/swap.h (SWAPSPACE2)
 */

#pragma once

#include <ir0/types.h>
#include <stddef.h>
#include <stdint.h>

struct process;

#define IR0_SWAP_PAGE_SIZE 4096U
#define IR0_SWAP_MAX_AREAS 8U
#define IR0_SWAP_MAX_EXTENTS 128U
#define IR0_SWAP_MAX_PAGES (1U << 16)
#define IR0_SWAP_MAGIC "SWAPSPACE2"

#define SWAP_FLAG_PREFER 0x8000
#define SWAP_FLAG_PRIO_MASK 0x7fff
#define SWAP_FLAG_PRIO_SHIFT 0

enum ir0_swap_kind
{
	IR0_SWAP_KIND_NONE = 0,
	IR0_SWAP_KIND_BLOCK,
	IR0_SWAP_KIND_FILE
};

struct ir0_swap_extent
{
	uint64_t file_page;
	uint64_t phys_lba;
	uint32_t nr_pages;
};

struct ir0_swap_header_info
{
	uint32_t version;
	uint32_t last_page;
	uint32_t nr_badpages;
};

struct ir0_swap_stats
{
	uint64_t totalswap_pages;
	uint64_t freeswap_pages;
	uint32_t areas;
};

void ir0_swap_init(void);

int ir0_swap_parse_header(const void *page, size_t page_size,
			  struct ir0_swap_header_info *out);

int ir0_swap_area_install(const char *path, int kind, int flags, dev_t dev,
			  const struct ir0_swap_extent *extents, size_t nextent,
			  uint32_t last_page);
int ir0_swap_area_remove(const char *path);
int ir0_swap_path_busy(const char *path);

int ir0_swap_slot_alloc(unsigned *type, uint64_t *offset);
int ir0_swap_slot_get(unsigned type, uint64_t offset);
int ir0_swap_slot_put(unsigned type, uint64_t offset);
int ir0_swap_slot_busy(unsigned type);

int ir0_swap_read_page(unsigned type, uint64_t offset, void *buf);
int ir0_swap_write_page(unsigned type, uint64_t offset, const void *buf);

int ir0_swap_out_page(struct process *proc, uintptr_t va);
int ir0_swap_in_page(struct process *proc, uintptr_t va, uint64_t map_flags);
int ir0_swap_dup_pte(uint64_t pte);
int ir0_swap_drop_pte(uint64_t pte);
int ir0_swap_drain_area(unsigned type);
int ir0_swap_try_reclaim(uint64_t target, uint64_t *reclaimed);

void ir0_swap_get_stats(struct ir0_swap_stats *out);
int ir0_swap_format_proc(char *buf, size_t count);
