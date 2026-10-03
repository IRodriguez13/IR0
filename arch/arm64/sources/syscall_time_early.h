/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/syscall_id.h>

int64_t arm64_early_time_syscall(void *context, enum ir0_syscall_id id,
				 uint64_t a0, uint64_t a1, uint64_t a2,
				 uint64_t a3, uint64_t a4, uint64_t a5);
int arm64_early_time_smoke_ok(void);
