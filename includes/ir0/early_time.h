/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/syscall_table.h>

extern const struct syscall_context_provider early_time_provider;
int early_time_smoke_ok(void);
