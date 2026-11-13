/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault_kernel.c
 * Description: Kernel-mode faults: uaccess faults kill the task, anything else panics.
 */

#include <ir0/oops.h>
#include <ir0/fault.h>
#include "page_fault_internal.h"
#include <mm/uaccess/user_va.h>

void pf_kernel_fault(const struct page_fault_info *info, uint64_t fault_addr)
{
	process_t *cur = process_get_current();
	unsigned int pid = cur ? (unsigned int)cur->task.pid : 0;
	const char *comm = cur ? cur->comm : "(none)";
	struct ir0_fault fault;

	ir0_fault_from_page_fault(&fault, info);
	fault.addr = (uintptr_t)fault_addr;

	if (mm_user_va_ok((uintptr_t)fault_addr, 1))
	{
		/* Kernel uaccess fault while serving a user task: kill it. */
		pf_diag_kernel_uaccess(&fault, pid, comm);
		if (cur && cur->mode == USER_MODE)
			signal_exit_current(SIGSEGV);
		panic("Unhandled kernel page fault (uaccess, no user task)");
	}

	pf_diag_kernel_fault(&fault, info, pid);

	/*
	 * Seed FAULT FRAME before panic() so a nested fault during the panic
	 * dump still leaves the primary fault site in the banner.
	 */
	panic_note_fault(&fault, pid, comm);
	panic("Unhandled kernel page fault");
}
