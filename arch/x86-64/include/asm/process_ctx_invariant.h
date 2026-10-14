/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

/* x86-64 canonical userspace range used by the Class-B return validator. */
#define IR0_USER_RIP_LO 0x00400000ULL
#define IR0_USER_RIP_HI 0x00007FFFFFFFFFFFULL

#ifndef IR0_USER_STACK_TOP
#define IR0_USER_STACK_TOP  0x7FFFF000ULL
#define IR0_USER_STACK_SIZE 0x100000ULL
#define IR0_USER_STACK_BASE (IR0_USER_STACK_TOP - IR0_USER_STACK_SIZE)
#endif

static inline int process_cs_rip_kernel_ret_bad(uint64_t cs, uint64_t rip)
{
	return (cs & 3u) == 0u && rip >= IR0_USER_RIP_LO &&
	       rip <= IR0_USER_RIP_HI;
}

static inline int process_rip_in_user_range(uint64_t rip)
{
	return rip >= IR0_USER_RIP_LO && rip <= IR0_USER_RIP_HI;
}

static inline int process_rip_in_user_stack(uint64_t rip)
{
	return rip >= IR0_USER_STACK_BASE && rip < IR0_USER_STACK_TOP;
}
