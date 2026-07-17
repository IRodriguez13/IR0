/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sched_switch.h
 * Description: Shared context-switch helper for scheduler backends.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "process.h"

/*
 * Shared path after a backend has selected @next.
 * Used by RR and priority-band backends (not a runqueue API).
 */
void sched_context_switch_to(process_t *next);
