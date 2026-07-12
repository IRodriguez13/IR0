/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exc_early.h
 * Description: ARM64 early VBAR / SVC / EL0 / IRQ hooks (F7.2–F7c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/** Install ir0_el1_vectors into VBAR_EL1. Returns 0 or -EINVAL. */
int arm64_vbar_early_install(void);

/** Trigger SVC #0 from EL1 (takes sync vector @ VBAR+0x200). */
void arm64_exc_trigger_svc(void);

/** C sync handler (Current EL SPx); prints ARM64_VBAR_OK on SVC. */
void arm64_exc_sync_el1(void);

/** C IRQ handler (Current EL SPx); EOI + timer disarm; ARM64_TIMER_IRQ_OK once. */
void arm64_exc_irq_el1(void);

/** Nonzero after first handled timer IRQ (boot poll). */
int arm64_timer_irq_seen(void);

/**
 * C sync handler (Lower EL AArch64). @frame is saved GPRs (x0 at [0], x8 at [8]).
 * Dispatches SVC; may retarget eret to arm64_after_el0 on exit.
 */
void arm64_exc_sync_lower(uint64_t *frame);

/** Drop to EL0 (eret); does not return via C. */
void arm64_enter_el0(void);

/** EL1 continuation after EL0 exit SVC (printed ARM64_EL0_RET_OK). */
void arm64_after_el0(void);

/** PSCI SYSTEM_OFF via HVC (QEMU virt). */
void arm64_psci_system_off(void);

/** PSCI SYSTEM_RESET via HVC (QEMU virt). */
void arm64_psci_system_reset(void);
