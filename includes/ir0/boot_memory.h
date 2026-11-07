/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — normalized early physical-memory inventory facade.
 *
 * Board/ISA code provides raw reserved-free RAM through arch_mm.h.  Common MM
 * consumes only the normalized inventory exposed here.
 */

#pragma once

#include <ir0/platform_resource.h>
#include <stddef.h>
#include <stdint.h>

#define IR0_BOOT_MEMORY_MAX_BANKS 32U
#define IR0_BOOT_MEMORY_MAX_RESERVATIONS 32U
#define IR0_BOOT_CMDLINE_MAX 256U

struct ir0_boot_snapshot
{
	struct ir0_phys_range usable[IR0_BOOT_MEMORY_MAX_BANKS];
	size_t usable_count;
	struct ir0_phys_range reserved[IR0_BOOT_MEMORY_MAX_RESERVATIONS];
	size_t reserved_count;
	struct ir0_phys_range firmware;
	struct ir0_phys_range initrd;
	struct ir0_phys_range framebuffer;
	char cmdline[IR0_BOOT_CMDLINE_MAX];
	int valid;
	int synthetic_memory;
};

int boot_memory_snapshot_init(uintptr_t firmware_handle);
const struct ir0_boot_snapshot *boot_memory_snapshot(void);
const char *boot_memory_cmdline(void);
int boot_memory_range_is_reserved(uintptr_t base, size_t size);

/* Read, page-normalize, order and validate the platform usable-RAM banks. */
int boot_memory_get_usable_ranges(struct ir0_phys_range *ranges,
				  size_t capacity, size_t *count);

/* Normalize a caller-supplied inventory without consulting an ISA backend. */
int boot_memory_normalize_ranges(const struct ir0_phys_range *input,
				 size_t input_count,
				 struct ir0_phys_range *output,
				 size_t output_capacity, size_t *output_count);

/* Remove page-expanded firmware reservations from normalized usable RAM. */
int boot_memory_exclude_reserved(const struct ir0_phys_range *usable,
				 size_t usable_count,
				 const struct ir0_phys_range *reserved,
				 size_t reserved_count,
				 struct ir0_phys_range *output,
				 size_t output_capacity, size_t *output_count);

/* Read the typed reservations supplied by the active architecture backend. */
int boot_memory_get_reserved_ranges(struct ir0_phys_range *ranges,
				    size_t capacity, size_t *count);

/* Build the final reserved-free inventory consumed by the generic PMM. */
int boot_memory_get_allocatable_ranges(struct ir0_phys_range *ranges,
				       size_t capacity, size_t *count);

/* Initialize the generic PMM from the normalized platform inventory. */
int boot_memory_init_pmm(void);
