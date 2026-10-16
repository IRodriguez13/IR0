/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/syscall_table.h>

extern const struct syscall_context_provider early_io_provider;
int early_io_smoke_ok(void);
