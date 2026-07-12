/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exc_early.h
 * Description: ARM64 early VBAR install + sync/SVC smoke hooks (F7.2).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/** Install ir0_el1_vectors into VBAR_EL1. Returns 0 or -EINVAL. */
int arm64_vbar_early_install(void);

/** Trigger SVC #0 from EL1 (takes sync vector @ VBAR+0x200). */
void arm64_exc_trigger_svc(void);

/** C sync handler (Current EL SPx); prints ARM64_VBAR_OK on SVC. */
void arm64_exc_sync_el1(void);
