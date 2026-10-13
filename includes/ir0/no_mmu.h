/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — common policy for ISA targets without an MMU.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

struct ir0_no_mmu_policy
{
	uintptr_t user_start;
	uintptr_t user_end;
	uintptr_t direct_start;
	uintptr_t direct_end;
	int (*physical_range_ok)(uintptr_t base, size_t size);
};

/* Install one backend-provided direct-address-space policy. */
int no_mmu_configure(const struct ir0_no_mmu_policy *policy);

/* Validate an accessible user or physical range under that policy. */
int no_mmu_user_range_ok(uintptr_t base, size_t size);
int no_mmu_physical_range_ok(uintptr_t base, size_t size);

/* Linux VM operations requiring distinct mappings are explicitly unsupported. */
int no_mmu_virtual_remap_status(void);
