/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_introspect.h
 * Description: Process list / fd-table introspection facade (KTM, /proc glue).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef struct ir0_proc_snapshot
{
	uint64_t processes;
	uint64_t zombies;
	uint64_t open_fds;
	uint64_t pipes;
} ir0_proc_snapshot_t;

int ir0_proc_snapshot_collect(ir0_proc_snapshot_t *out);
int32_t ir0_proc_current_pid(void);
