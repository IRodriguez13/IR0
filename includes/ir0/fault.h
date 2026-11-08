/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fault.h
 * Description: ISA-neutral description of a CPU fault. ISA backends decode
 *              their exception frame into struct ir0_fault; portable code
 *              (signals, panic, diagnostics) never reads vectors or ESR.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct page_fault_info;

/* What went wrong, independent of how the ISA numbers it. */
enum ir0_fault_kind
{
	IR0_FAULT_OTHER = 0,
	IR0_FAULT_PAGE,           /* translation or permission fault on an access */
	IR0_FAULT_PROTECTION,     /* x86 #GP / #NP */
	IR0_FAULT_STACK_SEGMENT,  /* x86 #SS */
	IR0_FAULT_ILLEGAL_INSN,   /* x86 #UD, ARM64 undefined / unknown EC */
	IR0_FAULT_ARITHMETIC,     /* x86 #DE/#OF/#XM, ARM64 FP trap */
	IR0_FAULT_ALIGNMENT,      /* ARM64 PC/SP alignment, x86 #AC */
	IR0_FAULT_BREAKPOINT,     /* ARM64 BRK, x86 #BP */
	IR0_FAULT_DOUBLE,         /* x86 #DF: never recoverable */
};

struct ir0_fault
{
	enum ir0_fault_kind kind;
	unsigned int hw_code;  /* raw ISA code, printed only: x86 vector, ARM64 ESR.EC */
	uintptr_t addr;        /* faulting data address (PAGE/ALIGNMENT), else 0 */
	uintptr_t ip;
	uintptr_t sp;
	uint64_t syndrome;     /* x86 error code, ARM64 ESR_EL1 */
	uint64_t status;       /* x86 RFLAGS, ARM64 SPSR_EL1 */
	uint64_t code_seg;     /* x86 CS; 0 without segmentation */
	uint64_t stack_seg;    /* x86 SS; 0 without segmentation */
	int present;
	int write;
	int exec;
};

/* Fill @fault from a decoded page fault (kind = IR0_FAULT_PAGE). */
void ir0_fault_from_page_fault(struct ir0_fault *fault,
			       const struct page_fault_info *info);

/* Stable upper-case name for logs ("PAGE_FAULT", "INVALID_OPCODE", ...). */
const char *ir0_fault_kind_name(enum ir0_fault_kind kind);
