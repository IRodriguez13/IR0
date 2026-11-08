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
#include <ir0/logging.h>
#include <ir0/fault.h>
#include "page_fault_internal.h"

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
		/*
		 * Kernel uaccess fault while serving a user task: kill the task
		 * with SIGSEGV. USER_FAULT_FRAME is logged at INFO so session
		 * smokes see the fault site, not only CONSOLE_SESSION_SEGV.
		 */
		ir0_log_user_fault(&fault, pid, comm);
		log_info_fmt("PF",
			     "CLASSIFY KERNEL_UACCESS_FAULT addr=%llx write=%x "
			     "pid=%x ip=%llx",
			     (unsigned long long)fault_addr,
			     (unsigned)(fault.write ? 1 : 0), pid,
			     (unsigned long long)fault.ip);
		if (cur && cur->mode == USER_MODE)
			signal_exit_current(SIGSEGV);
		panic("Unhandled kernel page fault (uaccess, no user task)");
	}

	log_error_fmt("PF",
		      "kernel_pf addr=%llx write=%x user=%x present=%x ip=%llx "
		      "sp=%llx pid=%x",
		      (unsigned long long)fault_addr,
		      (unsigned)(fault.write ? 1 : 0),
		      (unsigned)(info->user ? 1 : 0),
		      (unsigned)(fault.present ? 1 : 0),
		      (unsigned long long)fault.ip,
		      (unsigned long long)fault.sp, pid);
	if (fault_addr >= (uint64_t)(intptr_t)-4095)
		log_error_fmt("PF", "addr looks like ERR_PTR(-errno) errno=%llx",
			      (unsigned long long)(-(intptr_t)fault_addr));

	/*
	 * Seed FAULT FRAME before panic() so a nested fault during the panic
	 * dump still leaves the primary fault site in the banner.
	 */
	panic_note_fault(&fault, pid, comm);
	panic("Unhandled kernel page fault");
}
