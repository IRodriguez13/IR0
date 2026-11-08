/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: esr.h
 * Description: ESR_EL1 exception classes and abort ISS fields (Arm ARM
 *              D17.2.37), shared by the ARM64 exception and abort decoders.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#define ESR_EC_SHIFT      26
#define ESR_EC_MASK       0x3FUL

#define ESR_EC_UNKNOWN    0x00UL
#define ESR_EC_FPASIMD    0x07UL /* trapped FP/SIMD access */
#define ESR_EC_SVC64      0x15UL
#define ESR_EC_IABT_LOWER 0x20UL
#define ESR_EC_IABT_CUR   0x21UL
#define ESR_EC_PC_ALIGN   0x22UL
#define ESR_EC_DABT_LOWER 0x24UL
#define ESR_EC_DABT_CUR   0x25UL
#define ESR_EC_SP_ALIGN   0x26UL
#define ESR_EC_FP_EXC64   0x2CUL
#define ESR_EC_BRK64      0x3CUL

/* Data/instruction abort ISS. */
#define ESR_ISS_WNR       (1UL << 6)
#define ESR_ISS_CM        (1UL << 8)
#define ESR_ISS_FSC_MASK  0x3FUL

#define FSC_CLASS(fsc)    (((fsc) >> 2) & 0xFUL)
#define FSC_TRANSLATION   0x1UL
#define FSC_ACCESS_FLAG   0x2UL
#define FSC_PERMISSION    0x3UL

static inline uint64_t esr_ec(uint64_t esr)
{
	return (esr >> ESR_EC_SHIFT) & ESR_EC_MASK;
}

struct ir0_fault;

/*
 * Fill @fault from a non-abort synchronous exception (aborts go through
 * page_fault_decode()). @pc, @sp and @far come from the exception frame.
 */
void arm64_fault_from_esr(struct ir0_fault *fault, uint64_t esr, uint64_t pc,
			  uint64_t sp, uint64_t far, uint64_t spsr);
