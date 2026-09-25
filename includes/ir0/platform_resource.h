/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — architecture-neutral firmware resource descriptions.
 */

#pragma once

#include <stdint.h>

struct ir0_phys_range
{
	uint64_t base;
	uint64_t size;
};
