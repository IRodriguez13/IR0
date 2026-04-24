/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fault.c
 * Description: IR0 kernel source/header file
 */

#include <stdint.h>
#include <config.h>
#include <ir0/vga.h>
#include <mm/paging.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <kernel/process.h>
#include <string.h>
#include <mm/pmm.h>
#include <ir0/signals.h>

/* Deliver SIGSEGV to current process when a user fault is out of bounds */
static void pf_sigsegv_current(process_t *p)
{
	if (p)
		send_signal(p->task.pid, 11);
}

/*
 * Demand-filled pages are only allowed for addresses covered by the
 * process heap (brk), an mmap region, or the user stack.
 */
static int pf_fault_in_allowed_user_vma(process_t *p, uint64_t fa)
{
	struct mmap_region *r;

	if (fa >= USER_HEAP_BASE && fa < p->heap_end)
		return 1;
	if (fa >= (USER_STACK_TOP - USER_STACK_SIZE) && fa < USER_STACK_TOP)
		return 1;
	for (r = p->mmap_list; r != NULL; r = r->next) {
		uintptr_t base = (uintptr_t)r->addr;
		uint64_t end = base + (uint64_t)r->length;

		if (fa >= (uint64_t)base && fa < end)
			return 1;
	}
	return 0;
}

void page_fault_handler_x64(uint64_t *stack)
{
	uint64_t fault_addr;
	uint64_t errcode = stack[1];
	process_t *current;
	int not_present;
	int write;
	int user;
	int insn_fetch;

	asm volatile("mov %%cr2, %0" : "=r"(fault_addr));

	not_present = !(errcode & 1);
	write = errcode & 2;
	user = errcode & 4;
	(void)(errcode & 8);
	insn_fetch = (errcode & 16) != 0;

	/* Validate fault address is in userspace range (only for user mode faults) */
	const uint64_t USER_SPACE_START = 0x00400000UL;
	const uint64_t USER_SPACE_END = 0x00007FFFFFFFFFFFUL;

	if (user && not_present) {
		/*
		 * Includes null pointer (0): access to 0 triggers SIGSEGV.
		 */
		if (fault_addr < USER_SPACE_START || fault_addr > USER_SPACE_END) {
			current = process_get_current();
			if (current) {
				pf_sigsegv_current(current);
				return;
			}
			panic("[PF] Invalid userspace address");
		}

		current = process_get_current();
		if (!current || !current->page_directory)
			panic("[PF] No process context for user page fault");

		if (!pf_fault_in_allowed_user_vma(current, fault_addr)) {
			pf_sigsegv_current(current);
			return;
		}

		/* Allocate physical frame using PMM */
		uintptr_t phys_addr = pmm_alloc_frame();

		if (phys_addr == 0) {
			if (current) {
				pf_sigsegv_current(current);
				return;
			}
			panic("[PF] No hay memoria física para usuario");
		}

		/* Determine page flags */
		uint64_t flags = PAGE_USER;

		if (write)
			flags |= PAGE_RW;
		if (insn_fetch)
			flags |= PAGE_EXEC;

		/* Map page in process page directory */
		uint64_t vaddr_aligned = fault_addr & ~0xFFF;

		if (map_page_in_directory(current->page_directory, vaddr_aligned,
					   phys_addr, flags) != 0) {
			pmm_free_frame(phys_addr);
			if (current) {
				pf_sigsegv_current(current);
				return;
			}
			panic("[PF] Failed to map user page");
		}

		/*
		 * Zero out the newly allocated page; switch to process page
		 * directory to write to userspace.
		 */
		{
			uint64_t old_cr3 = get_current_page_directory();

			load_page_directory((uint64_t)current->page_directory);
			memset((void *)vaddr_aligned, 0, 0x1000);
			load_page_directory(old_cr3);
		}

		return;
	}

	/* Handle write protection fault (page present but not writable) */
	if (user && !not_present && write) {
		current = process_get_current();
		if (current) {
			pf_sigsegv_current(current);
			return;
		}
	}

	/* Kernel fault, reserved bit set, or other error - fatal */
	print("[PF] Kernel page fault en ");
	print_hex(fault_addr);
	print(" - código: ");
	print_hex(errcode);
	print(" not_present=");
	print_hex(not_present);
	print(" write=");
	print_hex(write);
	print(" user=");
	print_hex(user);
	print("\n");
	panic("Unhandled kernel page fault");
}

/* Double Fault */
void double_fault_x64(uint64_t error_code, uint64_t rip)
{
	print_colored("DOUBLE FAULT!\n", 0x0C, 0x00);
	print("Error code: ");
	print_hex(error_code);
	print("\n");
	print("RIP: ");
	print_hex(rip);
	print("\n");
	panic("Double fault - Kernel halted");
}

/* Triple Fault */
void triple_fault_x64()
{
	print_colored("TRIPLE FAULT!\n", 0x0C, 0x00);
	print("FATAL: CPU reset imminent\n");
	panic("Triple fault - System halted");
}

void general_protection_fault_x64(uint64_t error_code, uint64_t rip, uint64_t cs, uint64_t rsp)
{
	print_colored("GENERAL PROTECTION FAULT!\n", 0x0C, 0x00);
	print("Error code: ");
	print_hex(error_code);
	print("\n");
	print("RIP: ");
	print_hex(rip);
	print("\n");
	print("CS: ");
	print_hex(cs);
	print("\n");
	print("RSP: ");
	print_hex(rsp);
	print("\n");
	panic("GPF - Kernel halted");
}

void invalid_opcode_x64(uint64_t rip)
{
	print_colored("INVALID OPCODE!\n", 0x0C, 0x00);
	print("RIP: ");
	print_hex(rip);
	print("\n");
	panic("Invalid instruction - Kernel halted");
}

void divide_by_zero_x64(uint64_t rip)
{
	print_colored("DIVIDE BY ZERO!\n", 0x0C, 0x00);
	print("RIP: ");
	print_hex(rip);
	print("\n");
	panic("Divide by zero - Kernel halted");
}
