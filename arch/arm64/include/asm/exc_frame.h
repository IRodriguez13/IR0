/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exc_frame.h
 * Description: ARM64 exception frame saved by vectors.S (Linux pt_regs
 *              subset): x0..x30, SP_EL0, ELR_EL1, SPSR_EL1.
 *
 * The exception return restores SP_EL0/ELR_EL1/SPSR_EL1 from this frame, so a
 * context switch inside a handler cannot leak another task's return state.
 * Code that changes the user return state (signal delivery, syscall restart)
 * must write these slots, not the system registers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#define ARM64_EXC_FRAME_SP    248
#define ARM64_EXC_FRAME_ELR   256
#define ARM64_EXC_FRAME_SPSR  264
#define ARM64_EXC_FRAME_SIZE  272

/* Same layout as 64-bit word indices for C. */
#define ARM64_EXC_IDX_SP      (ARM64_EXC_FRAME_SP / 8)
#define ARM64_EXC_IDX_ELR     (ARM64_EXC_FRAME_ELR / 8)
#define ARM64_EXC_IDX_SPSR    (ARM64_EXC_FRAME_SPSR / 8)
