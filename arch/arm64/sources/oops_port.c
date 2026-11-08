/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: oops_port.c
 * Description: ARM64 panic hooks: fault registers under their AArch64 names.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops_port.h>
#include <ir0/fault.h>
#include <ir0/ktm/klog.h>

#include <stdint.h>

void ir0_oops_show_fault_regs(const struct ir0_fault *fault)
{
	if (!fault)
		return;
	klog_print("esr=");
	klog_hex64(fault->syndrome);
	klog_print(" ec=");
	klog_hex32(fault->hw_code);
	klog_print("\n");
	klog_print("pc=");
	klog_hex64((uint64_t)fault->ip);
	klog_print(" sp=");
	klog_hex64((uint64_t)fault->sp);
	klog_print(" spsr=");
	klog_hex64(fault->status);
	klog_print("\n");
	if (fault->kind == IR0_FAULT_PAGE || fault->kind == IR0_FAULT_ALIGNMENT)
	{
		klog_print("far=");
		klog_hex64((uint64_t)fault->addr);
		klog_print("\n");
	}
}
