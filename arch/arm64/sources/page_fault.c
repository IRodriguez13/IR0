/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault.c
 * Description: ARM64 instruction/data abort decode for the common MM fault
 *              handler (mm/page_fault.c).
 *
 * Reference: ARM DDI 0487 D17.2.40 (ESR_EL1 ISS for aborts) and Linux
 * arch/arm64/mm/fault.c (is_el0_instruction_abort, is_write_abort,
 * fault_info[] DFSC classes).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/page_fault.h>
#include <ir0/errno.h>
#include <ir0/fault.h>
#include <asm/esr.h>
#include <string.h>

int page_fault_decode(struct page_fault_info *out, uint64_t errcode,
		      void *irq_frame)
{
	uint64_t ec = esr_ec(errcode);
	uint64_t fsc = errcode & ESR_ISS_FSC_MASK;
	uint64_t far;
	uint64_t elr;
	uint64_t spsr;
	uint64_t sp_el0;
	int data;

	(void)irq_frame;
	if (!out)
		return -EINVAL;
	if (ec != ESR_EC_IABT_LOWER && ec != ESR_EC_IABT_CUR &&
	    ec != ESR_EC_DABT_LOWER && ec != ESR_EC_DABT_CUR)
		return -EINVAL;

	/* Read before anything can take another exception. */
	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));

	data = ec == ESR_EC_DABT_LOWER || ec == ESR_EC_DABT_CUR;
	out->address = (uintptr_t)far;
	out->ip = (uintptr_t)elr;
	out->sp = (uintptr_t)sp_el0;
	out->syndrome = errcode;
	out->status = spsr;
	out->code_seg = 0;
	out->stack_seg = 0;
	out->user = ec == ESR_EC_IABT_LOWER || ec == ESR_EC_DABT_LOWER;
	/* Cache maintenance reports WnR=1 but is a read (Linux is_write_abort). */
	out->write = data && (errcode & ESR_ISS_WNR) && !(errcode & ESR_ISS_CM);
	out->exec = !data;
	/* Only a translation fault means "no valid descriptor" (x86 P=0). */
	out->present = FSC_CLASS(fsc) == FSC_ACCESS_FLAG ||
		       FSC_CLASS(fsc) == FSC_PERMISSION;
	out->reserved = FSC_CLASS(fsc) != FSC_TRANSLATION &&
			FSC_CLASS(fsc) != FSC_ACCESS_FLAG &&
			FSC_CLASS(fsc) != FSC_PERMISSION;
	return 0;
}

void arm64_fault_from_esr(struct ir0_fault *fault, uint64_t esr, uint64_t pc,
			  uint64_t sp, uint64_t far, uint64_t spsr)
{
	uint64_t ec = esr_ec(esr);

	if (!fault)
		return;
	memset(fault, 0, sizeof(*fault));
	fault->hw_code = (unsigned int)ec;
	fault->syndrome = esr;
	fault->status = spsr;
	fault->ip = (uintptr_t)pc;
	fault->sp = (uintptr_t)sp;
	switch (ec)
	{
	case ESR_EC_BRK64:
		fault->kind = IR0_FAULT_BREAKPOINT;
		break;
	case ESR_EC_PC_ALIGN:
	case ESR_EC_SP_ALIGN:
		fault->kind = IR0_FAULT_ALIGNMENT;
		fault->addr = (uintptr_t)far;
		break;
	case ESR_EC_FP_EXC64:
		fault->kind = IR0_FAULT_ARITHMETIC;
		break;
	case ESR_EC_UNKNOWN:
		fault->kind = IR0_FAULT_ILLEGAL_INSN;
		break;
	default:
		fault->kind = IR0_FAULT_OTHER;
		break;
	}
}
