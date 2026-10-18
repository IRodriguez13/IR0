/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: product_ipc.h
 * Description: Product PIPE2 / SOCKETPAIR provider (ipc/ tables).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/syscall_table.h>

extern const struct syscall_context_provider ir0_product_ipc_provider;
