/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: no_mmu.c
 * Description: Common direct-address-space policy for no-MMU targets.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/errno.h>
#include <ir0/no_mmu.h>

static struct ir0_no_mmu_policy no_mmu_policy;
static int no_mmu_ready;

static int no_mmu_range_within(uintptr_t base, size_t size,
			       uintptr_t lower, uintptr_t upper)
{
	if (!no_mmu_ready || size == 0 || base < lower || base > UINTPTR_MAX - size)
		return 0;
	return base + size <= upper;
}

int no_mmu_configure(const struct ir0_no_mmu_policy *policy)
{
	if (!policy || !policy->physical_range_ok ||
	    policy->user_start >= policy->user_end ||
	    policy->direct_start >= policy->direct_end)
		return -EINVAL;
	if (no_mmu_ready)
		return -EBUSY;
	no_mmu_policy = *policy;
	no_mmu_ready = 1;
	return 0;
}

int no_mmu_user_range_ok(uintptr_t base, size_t size)
{
	if (!no_mmu_ready)
		return -ENODEV;
	return no_mmu_range_within(base, size, no_mmu_policy.user_start,
				    no_mmu_policy.user_end) ? 0 : -EFAULT;
}

int no_mmu_physical_range_ok(uintptr_t base, size_t size)
{
	if (!no_mmu_ready)
		return -ENODEV;
	if (!no_mmu_range_within(base, size, no_mmu_policy.direct_start,
				 no_mmu_policy.direct_end))
		return -EFAULT;
	return no_mmu_policy.physical_range_ok(base, size) ? 0 : -EFAULT;
}

int no_mmu_virtual_remap_status(void)
{
	return -ENOSYS;
}
