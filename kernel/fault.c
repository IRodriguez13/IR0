/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fault.c
 * Description: Helpers for the ISA-neutral struct ir0_fault.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/fault.h>
#include <ir0/page_fault.h>
#include <string.h>

void ir0_fault_from_page_fault(struct ir0_fault *fault,
			       const struct page_fault_info *info)
{
	if (!fault)
		return;
	memset(fault, 0, sizeof(*fault));
	fault->kind = IR0_FAULT_PAGE;
	if (!info)
		return;
	fault->addr = info->address;
	fault->ip = info->ip;
	fault->sp = info->sp;
	fault->syndrome = info->syndrome;
	fault->status = info->status;
	fault->code_seg = info->code_seg;
	fault->stack_seg = info->stack_seg;
	fault->present = info->present;
	fault->write = info->write;
	fault->exec = info->exec;
}

const char *ir0_fault_kind_name(enum ir0_fault_kind kind)
{
	switch (kind)
	{
	case IR0_FAULT_PAGE:
		return "PAGE_FAULT";
	case IR0_FAULT_PROTECTION:
		return "GENERAL_PROTECTION";
	case IR0_FAULT_STACK_SEGMENT:
		return "STACK_SEGMENT";
	case IR0_FAULT_ILLEGAL_INSN:
		return "INVALID_OPCODE";
	case IR0_FAULT_ARITHMETIC:
		return "ARITHMETIC";
	case IR0_FAULT_ALIGNMENT:
		return "ALIGNMENT";
	case IR0_FAULT_BREAKPOINT:
		return "BREAKPOINT";
	case IR0_FAULT_DOUBLE:
		return "DOUBLE_FAULT";
	case IR0_FAULT_OTHER:
	default:
		return "CPU_EXCEPTION";
	}
}
