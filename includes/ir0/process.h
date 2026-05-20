/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Thin facade toward `kernel/process.h` (`process_t`, FD tables, PID helpers).
 * Use `credentials.h` for permission checks (`ir0_current_cred`); reach this header
 * when the full task struct is required (e.g. `/proc`, CoW paths in `mm/`).
 */

#ifndef IR0_FACADE_PROCESS_H
#define IR0_FACADE_PROCESS_H

#include <kernel/process.h>

#endif /* IR0_FACADE_PROCESS_H */
