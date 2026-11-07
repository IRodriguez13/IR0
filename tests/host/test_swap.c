/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_swap.c
 * Description: Host contracts for SWAPSPACE2 parse, areas, and slots.
 *
 * Avoid <ir0/swap.h> here: it pulls IR0 dev_t into a libc compilation unit.
 */

#include "test_harness.h"

#include <stdint.h>
#include <string.h>

#define IR0_SWAP_PAGE_SIZE 4096U
#define IR0_SWAP_MAGIC "SWAPSPACE2"
#define SWAP_FLAG_PREFER 0x8000
#define IR0_SWAP_KIND_BLOCK 1

struct ir0_swap_header_info
{
	uint32_t version;
	uint32_t last_page;
	uint32_t nr_badpages;
};

struct ir0_swap_extent
{
	uint64_t file_page;
	uint64_t phys_lba;
	uint32_t nr_pages;
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
int ir0_swap_area_install(const char *path, int kind, int flags, uint32_t dev,
			  const struct ir0_swap_extent *extents, size_t nextent,
			  uint32_t last_page);
int ir0_swap_area_remove(const char *path);
int ir0_swap_path_busy(const char *path);
int ir0_swap_slot_alloc(unsigned *type, uint64_t *offset);
int ir0_swap_slot_put(unsigned type, uint64_t offset);
int ir0_swap_slot_busy(unsigned type);
void ir0_swap_get_stats(struct ir0_swap_stats *out);
int ir0_swap_format_proc(char *buf, size_t count);

static void fill_swap_header(unsigned char *page, uint32_t last_page)
{
	memset(page, 0, IR0_SWAP_PAGE_SIZE);
	page[1024] = 1;
	page[1028] = (unsigned char)(last_page & 0xffU);
	page[1029] = (unsigned char)((last_page >> 8) & 0xffU);
	page[1030] = (unsigned char)((last_page >> 16) & 0xffU);
	page[1031] = (unsigned char)((last_page >> 24) & 0xffU);
	memcpy(page + IR0_SWAP_PAGE_SIZE - 10, IR0_SWAP_MAGIC, 10);
}

void test_swap_header_and_areas(void)
{
	unsigned char page[IR0_SWAP_PAGE_SIZE];
	struct ir0_swap_header_info hdr;
	struct ir0_swap_extent ext;
	struct ir0_swap_stats st;
	char proc[256];
	unsigned type;
	uint64_t offset;

	ir0_swap_init();

	TEST_BEGIN("swap_header_rejects_bad_magic");
	memset(page, 0, sizeof(page));
	ASSERT(ir0_swap_parse_header(page, sizeof(page), &hdr) == -22);
	TEST_END();

	TEST_BEGIN("swap_header_parses_swapspace2");
	fill_swap_header(page, 16);
	ASSERT(ir0_swap_parse_header(page, sizeof(page), &hdr) == 0);
	ASSERT_EQ(hdr.version, 1U);
	ASSERT_EQ(hdr.last_page, 16U);
	TEST_END();

	TEST_BEGIN("swap_area_install_priority_and_proc");
	ext.file_page = 0;
	ext.phys_lba = 0;
	ext.nr_pages = 17;
	ASSERT(ir0_swap_area_install("/dev/hda2", IR0_SWAP_KIND_BLOCK,
				     SWAP_FLAG_PREFER | 5, 1, &ext, 1, 16) ==
	       0);
	ASSERT(ir0_swap_path_busy("/dev/hda2") == 1);
	ASSERT(ir0_swap_area_install("/dev/hda2", IR0_SWAP_KIND_BLOCK, 0, 1,
				     &ext, 1, 16) == -16);
	ir0_swap_get_stats(&st);
	ASSERT_EQ(st.areas, 1U);
	ASSERT_EQ(st.totalswap_pages, 16ULL);
	ASSERT(ir0_swap_format_proc(proc, sizeof(proc)) > 0);
	ASSERT(strstr(proc, "/dev/hda2") != NULL);
	ASSERT(strstr(proc, "partition") != NULL);
	TEST_END();

	TEST_BEGIN("swap_slot_alloc_put");
	ASSERT(ir0_swap_slot_alloc(&type, &offset) == 0);
	ASSERT_EQ(type, 0U);
	ASSERT(offset >= 1ULL);
	ASSERT(ir0_swap_slot_busy(type) == 1);
	ASSERT(ir0_swap_slot_put(type, offset) == 0);
	TEST_END();

	TEST_BEGIN("swap_area_remove_idle");
	ASSERT(ir0_swap_area_remove("/dev/hda2") == 0);
	ASSERT(ir0_swap_path_busy("/dev/hda2") == 0);
	ASSERT(ir0_swap_area_remove("/dev/hda2") == -22);
	TEST_END();
}
