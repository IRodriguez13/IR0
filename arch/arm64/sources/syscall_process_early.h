/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/syscall_table.h>

extern const struct syscall_context_provider arm64_early_process_provider;
int arm64_early_process_smoke_ok(void);
