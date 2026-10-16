/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

struct early_mm_layout
{
	uint64_t primary_heap_base;
	uint64_t primary_map_limit;
	uint64_t secondary_heap_base;
	uint64_t secondary_map_base;
	uint64_t secondary_map_limit;
};

extern const struct syscall_context_provider early_mm_provider;
void early_mm_reset_secondary_heap(const struct early_mm_layout *layout);
