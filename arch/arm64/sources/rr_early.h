/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: rr_early.h
 * Description: Freestanding rr_sched + process_t smoke for ARM64.
 */

#pragma once

/** Queue two process_t via the scheduler facade and switch successfully. */
int arm64_rr_sched_smoke(void);

/** Nonzero while timer IRQ should call the scheduler facade (WS-D smoke). */
int arm64_rr_tick_sched_active(void);
