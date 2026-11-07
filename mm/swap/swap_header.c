/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: swap_header.c
 * Description: SWAPSPACE2 header parse (Linux include/uapi/linux/swap.h).
 */

#include <ir0/errno.h>
#include <ir0/swap.h>
#include <string.h>

int ir0_swap_parse_header(const void *page, size_t page_size,
			  struct ir0_swap_header_info *out)
{
	const unsigned char *p = page;
	uint32_t version;
	uint32_t last_page;
	uint32_t nr_bad;

	if (!page || !out || page_size < IR0_SWAP_PAGE_SIZE)
		return -EINVAL;
	if (memcmp(p + page_size - 10, IR0_SWAP_MAGIC, 10) != 0)
		return -EINVAL;

	version = (uint32_t)p[1024] | ((uint32_t)p[1025] << 8) |
		  ((uint32_t)p[1026] << 16) | ((uint32_t)p[1027] << 24);
	last_page = (uint32_t)p[1028] | ((uint32_t)p[1029] << 8) |
		    ((uint32_t)p[1030] << 16) | ((uint32_t)p[1031] << 24);
	nr_bad = (uint32_t)p[1032] | ((uint32_t)p[1033] << 8) |
		 ((uint32_t)p[1034] << 16) | ((uint32_t)p[1035] << 24);

	if (version != 1U)
		return -EINVAL;
	if (last_page == 0U || last_page >= IR0_SWAP_MAX_PAGES)
		return -EINVAL;
	if (nr_bad != 0U)
		return -EINVAL;

	out->version = version;
	out->last_page = last_page;
	out->nr_badpages = nr_bad;
	return 0;
}
