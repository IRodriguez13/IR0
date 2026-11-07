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

#define ESR_EC_SHIFT      26
#define ESR_EC_MASK       0x3FUL
#define ESR_EC_IABT_LOW   0x20UL
#define ESR_EC_IABT_CUR   0x21UL
#define ESR_EC_DABT_LOW   0x24UL
#define ESR_EC_DABT_CUR   0x25UL
#define ESR_ISS_WNR       (1UL << 6)
#define ESR_ISS_CM        (1UL << 8)
#define ESR_ISS_FSC_MASK  0x3FUL
/* Fault status code class, FSC[5:2]. */
#define FSC_CLASS(fsc)    (((fsc) >> 2) & 0xFUL)
#define FSC_TRANSLATION   0x1UL
#define FSC_ACCESS_FLAG   0x2UL
#define FSC_PERMISSION    0x3UL

int page_fault_decode(struct page_fault_info *out, uint64_t errcode,
		      void *irq_frame)
{
	uint64_t ec = (errcode >> ESR_EC_SHIFT) & ESR_EC_MASK;
	uint64_t fsc = errcode & ESR_ISS_FSC_MASK;
	uint64_t far;
	uint64_t elr;
	uint64_t spsr;
	uint64_t sp_el0;
	int data;

	(void)irq_frame;
	if (!out)
		return -EINVAL;
	if (ec != ESR_EC_IABT_LOW && ec != ESR_EC_IABT_CUR &&
	    ec != ESR_EC_DABT_LOW && ec != ESR_EC_DABT_CUR)
		return -EINVAL;

	/* Read before anything can take another exception. */
	__asm__ volatile("mrs %0, far_el1" : "=r"(far));
	__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
	__asm__ volatile("mrs %0, sp_el0" : "=r"(sp_el0));

	data = ec == ESR_EC_DABT_LOW || ec == ESR_EC_DABT_CUR;
	out->address = (uintptr_t)far;
	out->ip = (uintptr_t)elr;
	out->sp = (uintptr_t)sp_el0;
	out->syndrome = errcode;
	out->status = spsr;
	out->code_seg = 0;
	out->stack_seg = 0;
	out->user = ec == ESR_EC_IABT_LOW || ec == ESR_EC_DABT_LOW;
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
