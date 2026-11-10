/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault.c
 * Description: Portable page fault policy: dispatcher, fault audit and SIGSEGV delivery.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops.h>
#include <ir0/fault.h>
#include "page_fault_internal.h"

void pf_user_segv(process_t *p, uint64_t *stack, uint64_t fault_addr,
			 const struct page_fault_info *info)
{
	struct ir0_fault fault;

	if (!p)
		panic("[PF] userspace fault without process");

	ir0_fault_from_page_fault(&fault, info);
	fault.addr = (uintptr_t)fault_addr;

	/* Logs USER_FAULT_FRAME before a handler runs or the task dies. */
	if (signal_deliver_fault(SIGSEGV, &fault, stack))
		return;

	pf_diag_user_segv(p, fault_addr, info);

	/*
	 * Linux wait status must be WIFSIGNALED(SIGSEGV), not exited(139).
	 * Ash prints "Segmentation fault" only when exit_signal is set.
	 */
	signal_exit_current(SIGSEGV);
}

void mm_page_fault_handle(const struct page_fault_info *info, void *irq_frame)
{
	uint64_t *stack = (uint64_t *)irq_frame;
	uint64_t fault_addr;
	process_t *current;
	int not_present;
	int write;
	int user;

	if (!info)
		return;

	pf_diag_fault_entry(info, stack);

	fault_addr = (uint64_t)info->address;
	not_present = !info->present;
	write = info->write;
	user = info->user;

	if (user && not_present)
	{
		pf_user_not_present(info, stack, fault_addr);
		return;
	}

	if (user && !not_present && write)
	{
		pf_user_write_present(info, stack, fault_addr);
		return;
	}

	/* A read fault the helper does not resolve falls through to SIGSEGV. */
	if (user && !not_present && !write &&
	    pf_user_read_present(info, stack, fault_addr))
		return;

	if (user)
	{
		current = process_get_current();
		if (current)
			pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	pf_kernel_fault(info, fault_addr);
}
