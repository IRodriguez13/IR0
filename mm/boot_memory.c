/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: boot_memory.c
 * Description: Generic firmware-memory inventory normalization and PMM handoff.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/arch_mm.h>
#include <ir0/boot_memory.h>
#include <ir0/errno.h>
#include <mm/pmm.h>

#include <stdint.h>

int boot_memory_normalize_ranges(const struct ir0_phys_range *input,
				 size_t input_count,
				 struct ir0_phys_range *output,
				 size_t output_capacity, size_t *output_count)
{
	size_t accepted = 0;
	size_t i;

	if (!input || !output || !output_count || input_count == 0 ||
	    input_count > output_capacity)
		return -EINVAL;

	for (i = 0; i < input_count; i++)
	{
		uint64_t base = input[i].base;
		uint64_t end;

		if (input[i].size == 0 || base > UINT64_MAX - input[i].size)
			return -EINVAL;
		end = base + input[i].size;
		if (base > UINT64_MAX - (PMM_FRAME_SIZE - 1U))
			return -EINVAL;
		base = (base + PMM_FRAME_SIZE - 1U) &
		       ~(uint64_t)(PMM_FRAME_SIZE - 1U);
		end &= ~(uint64_t)(PMM_FRAME_SIZE - 1U);
		if (end <= base)
			return -EINVAL;
		output[accepted].base = base;
		output[accepted].size = end - base;
		accepted++;
	}

	for (i = 1; i < accepted; i++)
	{
		size_t j = i;
		struct ir0_phys_range range = output[i];

		while (j > 0 && output[j - 1].base > range.base)
		{
			output[j] = output[j - 1];
			j--;
		}
		output[j] = range;
	}

	for (i = 1; i < accepted; i++)
	{
		uint64_t previous_end = output[i - 1].base + output[i - 1].size;

		if (previous_end < output[i - 1].base || output[i].base < previous_end)
			return -EINVAL;
	}

	*output_count = accepted;
	return 0;
}

int boot_memory_get_usable_ranges(struct ir0_phys_range *ranges,
				  size_t capacity, size_t *count)
{
	struct ir0_phys_range raw[IR0_BOOT_MEMORY_MAX_BANKS];
	size_t raw_count = mm_boot_usable_range_count();
	size_t i;

	if (!ranges || !count || raw_count > capacity ||
	    raw_count > IR0_BOOT_MEMORY_MAX_BANKS)
		return -EINVAL;
	if (raw_count == 0)
		return -ENODEV;
	for (i = 0; i < raw_count; i++)
	{
		if (mm_boot_usable_range(i, &raw[i]) != 0)
			return -EIO;
	}
	return boot_memory_normalize_ranges(raw, raw_count, ranges, capacity, count);
}

int boot_memory_init_pmm(void)
{
	struct ir0_phys_range ranges[IR0_BOOT_MEMORY_MAX_BANKS];
	size_t count;

	if (boot_memory_get_usable_ranges(ranges, IR0_BOOT_MEMORY_MAX_BANKS,
					  &count) != 0)
		return -ENODEV;
	return pmm_init_ranges(ranges, count);
}
