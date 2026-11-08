/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm.c
 * Description: x86-64 PML4 user/kernel half helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_mm.h>
#include <ir0/mm_range.h>
#include <ir0/errno.h>
#include <ir0/page_table_pool.h>
#include <ir0/pmm.h>
#include <ir0/video_backend.h>
#include <ir0/ahci_api.h>
#include <config.h>
#include <mm/paging.h>
#include <string.h>

uintptr_t mm_kernel_heap_start(void)
{
	return 0x00800000UL;
}

size_t mm_kernel_heap_size(void)
{
	return 0x01800000UL;
}

uintptr_t mm_user_va_start(void)
{
	return 0x00400000UL;
}

uintptr_t mm_user_va_end(void)
{
	return 0x00007FFFFFFFFFFFUL;
}

int mm_large_identity_supported(void)
{
	return 1;
}

unsigned mm_user_root_slots(void)
{
	return 256;
}

unsigned mm_root_slots(void)
{
	return 512;
}

void mm_copy_kernel_half(uint64_t *dst_root, const uint64_t *src_root)
{
	unsigned i;
	unsigned user_slots;
	unsigned total;

	if (!dst_root || !src_root)
		return;

	user_slots = mm_user_root_slots();
	total = mm_root_slots();
	for (i = user_slots; i < total; i++)
	{
		if (src_root[i] & PAGE_PRESENT)
			dst_root[i] = src_root[i];
	}
}

int mm_user_va_ok(uintptr_t addr, size_t size)
{
	/* ELF load floor … canonical low half (matches historical copy_user). */
	return mm_user_range_ok(addr, size, mm_user_va_start(),
				mm_user_va_end());
}

address_space_root_t mm_create_user_root(void)
{
	address_space_root_t root;
	uintptr_t kernel_root_phys;
	address_space_root_t kernel_root;
	const uint64_t supervisor_kbd_end = 0x00600000UL;

	root = (address_space_root_t)page_table_frame_alloc(
		MM_ALLOC_DIRECT_RECLAIM);
	if (!root)
	{
		paging_oom_note("mm_create_user_root", paging_oom_classify_current());
		return NULL;
	}
	memset(root, 0, PAGE_SIZE_4KB);

	/* Use the pinned boot root: a switch runs briefly on the old stack. */
	kernel_root_phys = paging_kernel_address_space();
	if (!kernel_root_phys)
	{
		kernel_root_phys = paging_current_address_space();
		paging_pin_kernel_address_space(kernel_root_phys);
	}
	kernel_root = (address_space_root_t)kernel_root_phys;
	mm_copy_kernel_half(root, kernel_root);

	/*
	 * Identity-map kernel heap and the PMM pool (PA==VA). Page-table
	 * frames live in PMM; mm_copy_kernel_half writes the process PML4
	 * through that window. Stop at USER_MMAP_START / PMM end so 2 MiB
	 * supervisor leaves do not cover the user mmap arena.
	 */
	if (map_supervisor_identity_low(root, 0, mm_user_va_start()) != 0 ||
	    map_supervisor_identity_low(root, KEYBOARD_BUFFER_ADDR,
					supervisor_kbd_end) != 0 ||
	    map_supervisor_identity_2mb(root, supervisor_kbd_end,
					PMM_PHYS_BASE + PMM_PHYS_SIZE) != 0 ||
	    mm_map_platform_supervisor(root) != 0)
	{
		paging_reclaim_user_tables(root);
		page_table_frame_free((uintptr_t)root);
		return NULL;
	}

	paging_ir0_mm_note_root_created((uintptr_t)root);
	return root;
}

void mm_destroy_user_root(address_space_root_t root)
{
	if (!root)
		return;
	paging_reclaim_user_tables(root);
	paging_ir0_mm_note_root_freed((uintptr_t)root);
	page_table_frame_free((uintptr_t)root);
}

void mm_prepare_root_activation(address_space_root_t root)
{
	if (root)
		paging_sync_kernel_mappings(root);
}

int mm_map_platform_supervisor(address_space_root_t root)
{
#if CONFIG_ENABLE_VBE
	uint32_t off;
	uint64_t fb_phys;
	uint32_t fb_size;

	if (root && video_backend_is_available())
	{
		fb_phys = video_backend_get_fb_phys();
		fb_size = video_backend_get_fb_size();
		if (fb_phys != 0 && fb_size != 0)
		{
			if (fb_size > (4U * 1024U * 1024U))
				fb_size = 4U * 1024U * 1024U;
			for (off = 0; off < fb_size; off += PAGE_SIZE_4KB)
			{
				uint64_t address = fb_phys + off;

				if (map_page_in_directory(root, address, address,
							  PAGE_PRESENT |
							  PAGE_RW) != 0)
					return -ENOMEM;
			}
		}
	}
#else
	(void)root;
#endif
	ahci_map_mmio_in_directory(root);
	return 0;
}

