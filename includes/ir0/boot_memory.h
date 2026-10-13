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

#define IR0_BOOT_MEMORY_MAX_BANKS 32U

/* Read, page-normalize, order and validate the platform usable-RAM banks. */
int boot_memory_get_usable_ranges(struct ir0_phys_range *ranges,
				  size_t capacity, size_t *count);

/* Normalize a caller-supplied inventory without consulting an ISA backend. */
int boot_memory_normalize_ranges(const struct ir0_phys_range *input,
				 size_t input_count,
				 struct ir0_phys_range *output,
				 size_t output_capacity, size_t *output_count);

/* Initialize the generic PMM from the normalized platform inventory. */
int boot_memory_init_pmm(void);
