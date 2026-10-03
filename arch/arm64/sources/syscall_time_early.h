/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

extern const struct syscall_context_provider arm64_early_time_provider;
int arm64_early_time_smoke_ok(void);
