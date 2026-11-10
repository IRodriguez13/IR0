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
#include <ir0/boot_port.h>
#include <ir0/errno.h>
#include <ir0/pmm.h>
#include <string.h>

/* Snapshot reservations plus the DTB, the initrd and the kernel heap. */
#define BOOT_MEMORY_MAX_PROTECTED (IR0_BOOT_MEMORY_MAX_RESERVATIONS + 3U)

static struct ir0_boot_snapshot g_boot_snapshot;

int boot_memory_snapshot_init(const struct ir0_boot_params *params)
{
	if (g_boot_snapshot.valid && g_boot_snapshot.usable_count > 0 &&
	    g_boot_snapshot.usable_count <= IR0_BOOT_MEMORY_MAX_BANKS &&
	    g_boot_snapshot.reserved_count <= IR0_BOOT_MEMORY_MAX_RESERVATIONS)
		return 0;
	memset(&g_boot_snapshot, 0, sizeof(g_boot_snapshot));
	if (ir0_boot_port_snapshot_fill(params, &g_boot_snapshot) != 0 ||
	    !g_boot_snapshot.valid || g_boot_snapshot.usable_count == 0 ||
	    g_boot_snapshot.usable_count > IR0_BOOT_MEMORY_MAX_BANKS ||
	    g_boot_snapshot.reserved_count > IR0_BOOT_MEMORY_MAX_RESERVATIONS)
	{
		memset(&g_boot_snapshot, 0, sizeof(g_boot_snapshot));
		return -ENODEV;
	}
	return 0;
}

const struct ir0_boot_snapshot *boot_memory_snapshot(void)
{
	return g_boot_snapshot.valid ? &g_boot_snapshot : NULL;
}

const char *boot_memory_cmdline(void)
{
	return g_boot_snapshot.valid ? g_boot_snapshot.cmdline : "";
}

int boot_memory_range_is_reserved(uintptr_t base, size_t size)
{
	uint64_t end;
	size_t i;

	if (!g_boot_snapshot.valid || size == 0 ||
	    (uint64_t)base > UINT64_MAX - size)
		return 0;
	end = (uint64_t)base + size;
	for (i = 0; i < g_boot_snapshot.reserved_count; i++)
	{
		uint64_t reserved_base = g_boot_snapshot.reserved[i].base;
		uint64_t reserved_size = g_boot_snapshot.reserved[i].size;
		uint64_t reserved_end;

		if (reserved_size == 0 ||
		    reserved_base > UINT64_MAX - reserved_size)
			continue;
		reserved_end = reserved_base + reserved_size;
		if ((uint64_t)base < reserved_end && reserved_base < end)
			return 1;
	}
	return 0;
}

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
	size_t raw_count;
	size_t i;

	if (!g_boot_snapshot.valid)
		return -ENODEV;
	raw_count = g_boot_snapshot.usable_count;
	if (!ranges || !count || raw_count > capacity ||
	    raw_count > IR0_BOOT_MEMORY_MAX_BANKS)
		return -EINVAL;
	for (i = 0; i < raw_count; i++)
		raw[i] = g_boot_snapshot.usable[i];
	return boot_memory_normalize_ranges(raw, raw_count, ranges, capacity, count);
}

int boot_memory_exclude_reserved(const struct ir0_phys_range *usable,
				 size_t usable_count,
				 const struct ir0_phys_range *reserved,
				 size_t reserved_count,
				 struct ir0_phys_range *output,
				 size_t output_capacity, size_t *output_count)
{
	struct ir0_phys_range current[IR0_BOOT_MEMORY_MAX_BANKS];
	struct ir0_phys_range next[IR0_BOOT_MEMORY_MAX_BANKS];
	size_t current_count;
	size_t reservation_index;
	size_t i;

	if (!usable || !output || !output_count || usable_count == 0 ||
	    usable_count > IR0_BOOT_MEMORY_MAX_BANKS ||
	    reserved_count > IR0_BOOT_MEMORY_MAX_RESERVATIONS ||
	    (reserved_count != 0 && !reserved))
		return -EINVAL;
	if (boot_memory_normalize_ranges(usable, usable_count, current,
					 IR0_BOOT_MEMORY_MAX_BANKS,
					 &current_count) != 0)
		return -EINVAL;

	for (reservation_index = 0; reservation_index < reserved_count;
	     reservation_index++)
	{
		uint64_t reserved_start = reserved[reservation_index].base;
		uint64_t reserved_end;
		size_t next_count = 0;

		if (reserved[reservation_index].size == 0 ||
		    reserved_start > UINT64_MAX - reserved[reservation_index].size)
			return -EINVAL;
		reserved_end = reserved_start + reserved[reservation_index].size;
		reserved_start &= ~(uint64_t)(PMM_FRAME_SIZE - 1U);
		if ((reserved_end & (PMM_FRAME_SIZE - 1U)) != 0U)
		{
			if (reserved_end > UINT64_MAX - (PMM_FRAME_SIZE - 1U))
				return -EINVAL;
			reserved_end = (reserved_end + PMM_FRAME_SIZE - 1U) &
				       ~(uint64_t)(PMM_FRAME_SIZE - 1U);
		}

		for (i = 0; i < current_count; i++)
		{
			uint64_t start = current[i].base;
			uint64_t end = start + current[i].size;

			if (reserved_end <= start || reserved_start >= end)
			{
				if (next_count >= IR0_BOOT_MEMORY_MAX_BANKS)
					return -ENOSPC;
				next[next_count++] = current[i];
				continue;
			}
			if (start < reserved_start)
			{
				if (next_count >= IR0_BOOT_MEMORY_MAX_BANKS)
					return -ENOSPC;
				next[next_count].base = start;
				next[next_count].size = reserved_start - start;
				next_count++;
			}
			if (reserved_end < end)
			{
				if (next_count >= IR0_BOOT_MEMORY_MAX_BANKS)
					return -ENOSPC;
				next[next_count].base = reserved_end;
				next[next_count].size = end - reserved_end;
				next_count++;
			}
		}
		for (i = 0; i < next_count; i++)
			current[i] = next[i];
		current_count = next_count;
	}

	if (current_count == 0 || current_count > output_capacity)
		return -ENOSPC;
	for (i = 0; i < current_count; i++)
		output[i] = current[i];
	*output_count = current_count;
	return 0;
}

int boot_memory_get_reserved_ranges(struct ir0_phys_range *ranges,
				    size_t capacity, size_t *count)
{
	size_t raw_count;
	size_t i;

	if (!g_boot_snapshot.valid)
		return -ENODEV;
	raw_count = g_boot_snapshot.reserved_count;
	if (!ranges || !count || raw_count > capacity ||
	    raw_count > IR0_BOOT_MEMORY_MAX_RESERVATIONS)
		return -EINVAL;
	for (i = 0; i < raw_count; i++)
		ranges[i] = g_boot_snapshot.reserved[i];
	*count = raw_count;
	return 0;
}

static int ranges_intersect(uint64_t base, uint64_t size,
			    const struct ir0_phys_range *range)
{
	return range->size != 0 && base < range->base + range->size &&
	       range->base < base + size;
}

static void protected_add(struct ir0_phys_range *ranges, size_t *count,
			  uint64_t base, uint64_t size)
{
	if (size == 0 || base > UINT64_MAX - size ||
	    *count >= BOOT_MEMORY_MAX_PROTECTED)
		return;
	ranges[*count].base = base;
	ranges[*count].size = size;
	(*count)++;
}

/*
 * Physical ranges the kernel keeps using after the PMM goes live: every
 * firmware/arch reservation, the devicetree blob, the initrd and the
 * fixed kernel heap window.
 */
static size_t boot_memory_protected_ranges(struct ir0_phys_range *ranges)
{
	size_t count = 0;
	size_t i;

	for (i = 0; i < g_boot_snapshot.reserved_count &&
		    i < IR0_BOOT_MEMORY_MAX_RESERVATIONS; i++)
		protected_add(ranges, &count, g_boot_snapshot.reserved[i].base,
			      g_boot_snapshot.reserved[i].size);
	protected_add(ranges, &count, g_boot_snapshot.dtb.base,
		      g_boot_snapshot.dtb.size);
	protected_add(ranges, &count, g_boot_snapshot.initrd.base,
		      g_boot_snapshot.initrd.size);
	protected_add(ranges, &count, (uint64_t)mm_kernel_heap_start(),
		      (uint64_t)mm_kernel_heap_size());
	return count;
}

int boot_memory_get_allocatable_ranges(struct ir0_phys_range *ranges,
				       size_t capacity, size_t *count)
{
	struct ir0_phys_range current[IR0_BOOT_MEMORY_MAX_BANKS];
	struct ir0_phys_range next[IR0_BOOT_MEMORY_MAX_BANKS];
	struct ir0_phys_range protected_ranges[BOOT_MEMORY_MAX_PROTECTED];
	size_t current_count;
	size_t protected_count;
	size_t done;
	size_t i;
	int ret;

	if (!ranges || !count)
		return -EINVAL;
	ret = boot_memory_get_usable_ranges(current, IR0_BOOT_MEMORY_MAX_BANKS,
					    &current_count);
	if (ret != 0)
		return ret;
	if (g_boot_snapshot.reserved_count > IR0_BOOT_MEMORY_MAX_RESERVATIONS)
		return -EINVAL;
	/*
	 * Linux memblock_reserve()s the kernel image, the initrd and the FDT
	 * before memblock frees pages to the buddy allocator. Do the same:
	 * the PMM only sees RAM that nothing else owns.
	 */
	protected_count = boot_memory_protected_ranges(protected_ranges);
	for (done = 0; done < protected_count;)
	{
		size_t chunk = protected_count - done;
		size_t next_count;

		if (chunk > IR0_BOOT_MEMORY_MAX_RESERVATIONS)
			chunk = IR0_BOOT_MEMORY_MAX_RESERVATIONS;
		ret = boot_memory_exclude_reserved(current, current_count,
						   &protected_ranges[done], chunk,
						   next, IR0_BOOT_MEMORY_MAX_BANKS,
						   &next_count);
		if (ret != 0)
			return ret;
		for (i = 0; i < next_count; i++)
			current[i] = next[i];
		current_count = next_count;
		done += chunk;
	}
	if (current_count > capacity)
		return -ENOSPC;
	for (i = 0; i < current_count; i++)
		ranges[i] = current[i];
	*count = current_count;
	return 0;
}

int boot_memory_init_pmm(void)
{
	struct ir0_phys_range ranges[IR0_BOOT_MEMORY_MAX_BANKS];
	size_t count;

	if (boot_memory_get_allocatable_ranges(ranges,
					       IR0_BOOT_MEMORY_MAX_BANKS,
					       &count) != 0)
		return -ENODEV;
	return pmm_init_ranges(ranges, count);
}

int boot_memory_check_pmm(void)
{
	struct ir0_phys_range protected_ranges[BOOT_MEMORY_MAX_PROTECTED];
	size_t protected_count;
	size_t frames;
	size_t frame;
	int overlaps = 0;

	if (!g_boot_snapshot.valid)
		return -ENODEV;
	frames = pmm_total_frames();
	if (frames == 0)
		return -ENODEV;
	protected_count = boot_memory_protected_ranges(protected_ranges);
	for (frame = 0; frame < frames; frame++)
	{
		uint64_t phys = (uint64_t)pmm_frame_phys(frame);
		size_t i;

		for (i = 0; i < protected_count; i++)
		{
			if (ranges_intersect(phys, PMM_FRAME_SIZE,
					     &protected_ranges[i]))
			{
				overlaps++;
				break;
			}
		}
	}
	return overlaps;
}
