/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: oops_port.c
 * Description: Weak defaults for the ISA panic hooks; ISA backends override
 *              them with their register names and extra dumps.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops_port.h>
#include <ir0/fault.h>
#include <ir0/ktm/klog.h>

#include <stdint.h>

__attribute__((weak)) void ir0_oops_dump_arch_extra(void)
{
}

__attribute__((weak)) void ir0_oops_show_fault_regs(const struct ir0_fault *fault)
{
	if (!fault)
		return;
	klog_print("status=");
	klog_hex64(fault->status);
	klog_print("\n");
}

__attribute__((weak)) const char *
ir0_oops_user_fault_extra(const struct ir0_fault *fault, struct process *proc,
			  const char *classify)
{
	(void)fault;
	(void)proc;
	return classify;
}
