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




#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <ir0/logging.h>
#include <mm/paging.h>
#include "page_fault_internal.h"

void pf_kernel_fault(const struct page_fault_info *info, uint64_t fault_addr)
{
	process_t *current;
	int not_present = !info->present;
	int write = info->write;
	int user = info->user;

	if (mm_user_va_ok((uintptr_t)fault_addr, 1))
	{
		current = process_get_current();

		/*
		 * Kernel uaccess #PF while serving a user task: kill the task
		 * with SIGSEGV. Must log USER_FAULT_FRAME (INFO) — DEBUG was
		 * invisible under the default klog profile and session smokes
		 * only saw CONSOLE_SESSION_SEGV with no fault site.
		 */
		ir0_log_user_fault_frame(
			14, (unsigned long long)fault_addr,
			(unsigned long long)info->syndrome,
			(unsigned long long)info->ip,
			(unsigned long long)info->code_seg,
			(unsigned long long)info->sp,
			info->present, info->write, info->exec,
			current ? (unsigned)((uint32_t)current->task.pid) : 0,
			current ? current->comm : "(none)");
		log_info_fmt("PF",
			      "CLASSIFY KERNEL_UACCESS_FAULT addr=%llx write=%x "
			      "pid=%x rip=%llx",
			      (unsigned long long)fault_addr,
			      (unsigned)(write ? 1 : 0),
			      (unsigned)(current ? (uint32_t)current->task.pid : 0),
			      (unsigned long long)info->ip);
		if (current && current->mode == USER_MODE)
		{
			if (!process_signal_default_kill(current, SIGSEGV))
			{
				current->exit_signal = SIGSEGV;
				process_exit(0);
			}
		}
		panic("Unhandled kernel page fault (uaccess, no user task)");
	}

	{
		uint64_t fault_rip = (uint64_t)info->ip;
		uint64_t fault_cs = info->code_seg;
		uint64_t fault_rflags = info->status;
		uint64_t fault_rsp = (uint64_t)info->sp;
		uint64_t fault_ss = info->stack_seg;
		process_t *cur = process_get_current();
		int stack_overflow = 0;

		print("[PF] Kernel page fault addr=");
		print_hex64(fault_addr);
		print(" write=");
		print_hex((uintptr_t)write);
		print(" user=");
		print_hex((uintptr_t)user);
		print(" present=");
		print_hex((uintptr_t)info->present);
		print(" rip=");
		print_hex64(fault_rip);
		print(" cs=");
		print_hex64(fault_cs);
		print(" rsp=");
		print_hex64(fault_rsp);
		print(" np=");
		print_hex((uintptr_t)not_present);
		print(" pid=");
		print_hex((uintptr_t)(cur ? (uint32_t)cur->task.pid : 0));
		print("\n");
		log_error_fmt("PF",
			       "kernel_pf addr=%llx write=%llx user=%llx rip=%llx cs=%llx rsp=%llx pid=%x",
			       (unsigned long long)fault_addr,
			       (unsigned long long)(write ? 1 : 0),
			       (unsigned long long)(user ? 1 : 0),
			       (unsigned long long)fault_rip,
			       (unsigned long long)fault_cs,
			       (unsigned long long)fault_rsp,
			       (unsigned)(cur ? (uint32_t)cur->task.pid : 0));
		if (fault_addr >= (uint64_t)(intptr_t)-4095 &&
		    fault_addr <= (uint64_t)(intptr_t)-1)
		{
			print("[PF] addr looks like ERR_PTR(-errno) errno=");
			print_hex((uintptr_t)(-(intptr_t)fault_addr));
			print("\n");
		}

		if (fault_rsp >= IR0_KSTACK_VA_BASE)
		{
			uint64_t off = fault_rsp - IR0_KSTACK_VA_BASE;
			uint64_t in_slot = off % (uint64_t)IR0_KSTACK_SLOT_SIZE;

			if (in_slot < (uint64_t)PAGE_SIZE_4KB)
				stack_overflow = 1;
		}

		/*
		 * Seed FAULT FRAME before panic() so a nested #DF during the
		 * panic dump still leaves the primary #PF site in the banner.
		 */
		panic_note_exception_frame(
			14, info->syndrome, fault_rip, fault_cs,
			fault_rflags, fault_rsp, fault_ss, stack_overflow,
			cur ? (uint32_t)cur->task.pid : 0,
			cur ? cur->comm : "(none)");
	}

	panic("Unhandled kernel page fault");
}
