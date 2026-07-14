/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: process_early.h
 * Description: Freestanding process+TTBR and fork/exec smokes.
 */

#pragma once

/** Two-task EL1 switch with distinct TTBR0 roots. Returns 0 on success. */
int arm64_process_ttbr_smoke(void);

/** Fork-like TTBR clone + exec-like entry replace. Returns 0 on success. */
int arm64_fork_exec_smoke(void);

/** task_t + switch_context_arm64 TTBR smoke → ARM64_PROCESS_T_SWITCH_OK. */
int arm64_process_t_switch_smoke(void);
