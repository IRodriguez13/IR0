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
#include <arch/common/arch_portable.h>
#include <ir0/copy_user.h>
#include <ir0/serial_io.h>

#define PF_USER_SPACE_START 0x00400000UL
#define PF_USER_SPACE_END   0x00007FFFFFFFFFFFUL

static int pf_addr_in_heap(process_t *p, uint64_t fa)
{
	if (!p)
		return 0;
	return (fa >= USER_HEAP_BASE && fa < p->heap_end);
}

static int pf_addr_in_stack(process_t *p, uint64_t fa)
{
	if (!p)
		return 0;
	return (fa >= (USER_STACK_TOP - USER_STACK_SIZE) && fa < USER_STACK_TOP);
}

static int pf_addr_in_mmap(process_t *p, uint64_t fa)
{
	struct mmap_region *r;

	if (!p)
		return 0;
	for (r = p->mmap_list; r != NULL; r = r->next)
	{
		uintptr_t base = (uintptr_t)r->addr;
		uint64_t end = base + (uint64_t)r->length;

		if (fa >= (uint64_t)base && fa < end)
			return 1;
	}
	return 0;
}

static int pf_addr_in_allowed_vma(process_t *p, uint64_t fa)
{
	return pf_addr_in_heap(p, fa) || pf_addr_in_stack(p, fa) ||
	       pf_addr_in_mmap(p, fa);
}

static void pf_audit_classify(uint64_t *stack, uint64_t fault_addr, uint64_t errcode)
{
	process_t *current = process_get_current();
	int not_present = !(errcode & 1);
	int write = (errcode & 2) != 0;
	int user = (errcode & 4) != 0;
	int reserved = (errcode & 8) != 0;
	int insn_fetch = (errcode & 16) != 0;
	uint64_t fault_rip = stack ? stack[2] : 0;
	uint64_t fault_cs = stack ? stack[3] : 0;
	uint64_t fault_rsp = stack ? stack[5] : 0;
	uint64_t *pte = NULL;
	uint64_t pte_flags = 0;
	int mapped = 0;
	int in_vma = 0;
	int in_userspace_range = 0;

	if (ir0_panic_in_progress())
		return;

	if (current && current->page_directory)
	{
		mapped = is_page_mapped_in_directory(current->page_directory,
						   fault_addr, &pte_flags);
		pte = paging_get_pte(current->page_directory,
				     (uintptr_t)(fault_addr & ~0xFFFULL));
	}

	in_vma = pf_addr_in_allowed_vma(current, fault_addr);
	in_userspace_range = (fault_addr >= PF_USER_SPACE_START &&
			      fault_addr <= PF_USER_SPACE_END);

	serial_print("[PF_AUDIT][FAULT] cr2=");
	serial_print_hex64(fault_addr);
	serial_print(" err=");
	serial_print_hex64(errcode);
	serial_print(" present=");
	serial_print_hex64(not_present ? 0 : 1);
	serial_print(" write=");
	serial_print_hex64(write ? 1 : 0);
	serial_print(" user=");
	serial_print_hex64(user ? 1 : 0);
	serial_print(" reserved=");
	serial_print_hex64(reserved ? 1 : 0);
	serial_print(" insn_fetch=");
	serial_print_hex64(insn_fetch ? 1 : 0);
	serial_print(" rip=");
	serial_print_hex64(fault_rip);
	serial_print(" cs=");
	serial_print_hex64(fault_cs);
	serial_print(" rsp=");
	serial_print_hex64(fault_rsp);
	serial_print(" mode=");
	serial_print(user ? "user" : "kernel");
	serial_print(" pid=");
	serial_print_hex32(current ? (uint32_t)current->task.pid : 0);
	serial_print(" comm=");
	serial_print(current ? current->comm : "(none)");
	serial_print("\n");

	serial_print("[PF_AUDIT][VMA] in_allowed_vma=");
	serial_print_hex64(in_vma ? 1 : 0);
	serial_print(" in_heap=");
	serial_print_hex64(pf_addr_in_heap(current, fault_addr) ? 1 : 0);
	serial_print(" in_stack=");
	serial_print_hex64(pf_addr_in_stack(current, fault_addr) ? 1 : 0);
	serial_print(" in_mmap=");
	serial_print_hex64(pf_addr_in_mmap(current, fault_addr) ? 1 : 0);
	serial_print(" pte_present=");
	serial_print_hex64((mapped > 0 && pte && (*pte & PAGE_PRESENT)) ? 1 : 0);
	serial_print(" pte_user=");
	serial_print_hex64(pte_flags & PAGE_USER ? 1 : 0);
	serial_print(" pte_rw=");
	serial_print_hex64(pte_flags & PAGE_RW ? 1 : 0);
	serial_print(" pte_nx=");
	serial_print_hex64(pte && (*pte & PAGE_NX) ? 1 : 0);
	serial_print("\n");

	if (!user && in_userspace_range)
	{
		serial_print("[PF_AUDIT][CLASSIFY] KERNEL_DEREF_USERPTR cr2_in_userspace=1\n");
	}

	if (user && not_present && in_vma && mapped <= 0)
	{
		serial_print("[PF_AUDIT][CLASSIFY] PF_ADDR_IN_VMA_NOT_MAPPED\n");
	}
	else if (user && not_present && !in_vma)
	{
		serial_print("[PF_AUDIT][CLASSIFY] PF_ADDR_NOT_IN_VMA\n");
	}
	else if (user && not_present && in_vma)
	{
		serial_print("[PF_AUDIT][CLASSIFY] USER_PF_SHOULD_BE_HANDLED\n");
	}

	if (!user && fault_rip != 0)
	{
		serial_print("[PF_AUDIT][CLASSIFY] kernel_fault_rip=");
		serial_print_hex64(fault_rip);
		serial_print("\n");
	}
}

/* Deliver SIGSEGV-equivalent termination for a faulting userspace process. */
static __attribute__((noreturn)) void pf_terminate_userspace(process_t *p,
							      uint64_t fault_addr,
							      uint64_t errcode)
{
	if (!p)
		panic("[PF] userspace fault without process");

	serial_print("[PF] userspace segv pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" addr=");
	serial_print_hex64(fault_addr);
	serial_print(" err=");
	serial_print_hex64(errcode);
	serial_print("\n");

	(void)send_signal(p->task.pid, SIGSEGV);
	process_exit(128 + SIGSEGV);
}

/*
 * Demand-filled pages are only allowed for addresses covered by the
 * process heap (brk), an mmap region, or the user stack.
 */
static int pf_fault_in_allowed_user_vma(process_t *p, uint64_t fa)
{
	return pf_addr_in_allowed_vma(p, fa);
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

	if (ir0_panic_in_progress())
	{
		cpu_relax();
		return;
	}

	not_present = !(errcode & 1);
	write = errcode & 2;
	user = errcode & 4;
	(void)(errcode & 8);
	insn_fetch = (errcode & 16) != 0;

	pf_audit_classify(stack, fault_addr, errcode);

	/* Validate fault address is in userspace range (only for user mode faults) */
	const uint64_t USER_SPACE_START = PF_USER_SPACE_START;
	const uint64_t USER_SPACE_END = PF_USER_SPACE_END;

	if (user && not_present) {
		/*
		 * Includes null pointer (0): access to 0 triggers SIGSEGV.
		 */
		if (fault_addr < USER_SPACE_START || fault_addr > USER_SPACE_END) {
			current = process_get_current();
			if (current) {
				pf_terminate_userspace(current, fault_addr, errcode);
			}
			return;
		}

		current = process_get_current();
		if (!current || !current->page_directory)
			return;

		if (!pf_fault_in_allowed_user_vma(current, fault_addr)) {
			pf_terminate_userspace(current, fault_addr, errcode);
		}

		/* Allocate physical frame using PMM */
		uintptr_t phys_addr = pmm_alloc_frame();

		if (phys_addr == 0) {
			if (current) {
				pf_terminate_userspace(current, fault_addr, errcode);
			}
			return;
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
				pf_terminate_userspace(current, fault_addr, errcode);
			}
			return;
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
			pf_terminate_userspace(current, fault_addr, errcode);
		}
	}

	if (user) {
		current = process_get_current();
		if (current)
			pf_terminate_userspace(current, fault_addr, errcode);
		return;
	}

	/* Kernel fault, reserved bit set, or other error - fatal */
	if (ir0_panic_in_progress())
	{
		cpu_relax();
		return;
	}

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

void gpf_audit_from_isr(uint64_t *stack)
{
	process_t *current = process_get_current();
	uint64_t errcode = stack ? stack[1] : 0;
	uint64_t fault_rip = stack ? stack[2] : 0;
	uint64_t fault_cs = stack ? stack[3] : 0;
	uint64_t fault_rflags = stack ? stack[4] : 0;
	uint64_t fault_rsp = stack ? stack[5] : 0;
	uint64_t fault_ss = stack ? stack[6] : 0;
	int user = (fault_cs & 3U) == 3U;
	extern uint64_t iretq_checkpoint_buf[40];
	uint64_t ckpt_rip = iretq_checkpoint_buf[2];
	uint64_t ckpt_cs = iretq_checkpoint_buf[3];
	uint64_t ckpt_rsp = iretq_checkpoint_buf[5];

	if (ir0_panic_in_progress())
		return;

	serial_print("[GPF_AUDIT][FAULT] err=");
	serial_print_hex64(errcode);
	serial_print(" rip=");
	serial_print_hex64(fault_rip);
	serial_print(" cs=");
	serial_print_hex64(fault_cs);
	serial_print(" rsp=");
	serial_print_hex64(fault_rsp);
	serial_print(" ss=");
	serial_print_hex64(fault_ss);
	serial_print(" rflags=");
	serial_print_hex64(fault_rflags);
	serial_print(" mode=");
	serial_print(user ? "user" : "kernel");
	serial_print(" pid=");
	serial_print_hex32(current ? (uint32_t)current->task.pid : 0);
	serial_print(" comm=");
	serial_print(current ? current->comm : "(none)");
	serial_print(" cr3=");
	serial_print_hex64(get_current_page_directory());
	serial_print("\n");

	serial_print("[GPF_AUDIT][IRETQ_CKPT] rip=");
	serial_print_hex64(ckpt_rip);
	serial_print(" cs=");
	serial_print_hex64(ckpt_cs);
	serial_print(" rsp=");
	serial_print_hex64(ckpt_rsp);
	serial_print("\n");

	if (!user)
	{
		serial_print("[GPF_AUDIT][CLASSIFY] GPF_IN_KERNEL_BEFORE_IRET\n");
		if (fault_rip == ckpt_rip || (fault_rip >= 0x160000ULL && fault_rip <= 0x170000ULL))
		{
			serial_print("[GPF_AUDIT][CLASSIFY] GPF_DURING_IRETQ "
			             "note=rip_near_arch_switch_to_user\n");
		}
	}
	else
	{
		serial_print("[GPF_AUDIT][CLASSIFY] GPF_IN_USERSPACE\n");
	}
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
