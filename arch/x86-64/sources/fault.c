/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fault.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <config.h>
#include <ir0/vga.h>
#include <ir0/paging.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <string.h>
#include <ir0/pmm.h>
#include <ir0/signals.h>
#include <arch/common/arch_portable.h>
#include <ir0/copy_user.h>
#include <ir0/serial_io.h>
#include <ktm.h>
#include <ktm_probe_diag.h>
#include <d1_13_malloc_pf_diag.h>

#define PF_USER_SPACE_START 0x00400000UL
#define PF_USER_SPACE_END   0x00007FFFFFFFFFFFUL

/* mmap(2) protection bits (mirror of includes/ir0/syscall.h). */
#ifndef PROT_READ
#define PROT_READ  0x1
#endif
#ifndef PROT_WRITE
#define PROT_WRITE 0x2
#endif
#ifndef PROT_EXEC
#define PROT_EXEC  0x4
#endif

static int pf_addr_in_heap(process_t *p, uint64_t fa)
{
	uint64_t heap_lo;

	if (!p)
		return 0;

	/*
	 * Post-exec heap lives at PT_LOAD brk (often 0x405000+), not USER_HEAP_BASE.
	 * Demand-fill on first touch after brk/sbrk must honor [heap_start, heap_end).
	 */
	heap_lo = (uint64_t)p->heap_start;
	if (heap_lo == 0)
		heap_lo = USER_HEAP_BASE;

	return (fa >= heap_lo && fa < (uint64_t)p->heap_end);
}

static int pf_addr_in_stack(process_t *p, uint64_t fa)
{
	if (!p)
		return 0;
	return (fa >= (USER_STACK_TOP - USER_STACK_SIZE) && fa < USER_STACK_TOP);
}

static struct mmap_region *pf_mmap_region_for(process_t *p, uint64_t fa)
{
	struct mmap_region *r;

	if (!p)
		return NULL;
	for (r = p->mmap_list; r != NULL; r = r->next)
	{
		uintptr_t base = (uintptr_t)r->addr;
		uint64_t end = base + (uint64_t)r->length;

		if (fa >= (uint64_t)base && fa < end)
			return r;
	}
	return NULL;
}

static int pf_addr_in_mmap(process_t *p, uint64_t fa)
{
	return pf_mmap_region_for(p, fa) != NULL;
}

static int pf_addr_in_allowed_vma(process_t *p, uint64_t fa)
{
	return pf_addr_in_heap(p, fa) || pf_addr_in_stack(p, fa) ||
	       pf_addr_in_mmap(p, fa);
}

/*
 * D1.10 — stack-adjacent PF diagnostics (no policy change).
 * GPR layout: frame[-1]=rax .. frame[-6]=rsi frame[-7]=rdi (isr stub).
 */
static void pf_d110_stack_adjacent_diag(uint64_t *frame, uint64_t fault_addr,
					uint64_t errcode, process_t *p)
{
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rip;
	uint64_t rsp;
	uint64_t movsq_end;
	int write_fault;
	int src_touch;
	int dst_touch;
	struct mmap_region *r;
	struct mmap_region *prev_mmap;
	struct mmap_region *next_mmap;
	uint64_t prev_end;
	uint64_t next_start;
	uintptr_t stack_lo;
	uintptr_t stack_hi;
	uintptr_t guard_lo;

	if (!frame || !p || !(errcode & 4))
		return;

	stack_lo = (uintptr_t)p->stack_start;
	stack_hi = (uintptr_t)(p->stack_start + p->stack_size);
	guard_lo = stack_lo - PAGE_SIZE_4KB;

	if (fault_addr < guard_lo - PAGE_SIZE_4KB ||
	    fault_addr >= stack_hi + PAGE_SIZE_4KB)
		return;

	rax = frame[-1];
	rcx = frame[-2];
	rdx = frame[-3];
	rbx = frame[-4];
	rbp = frame[-5];
	rsi = frame[-6];
	rdi = frame[-7];
	rip = frame[2];
	rsp = frame[5];
	write_fault = (errcode & 2) != 0;

	movsq_end = 0;
	if (rcx > 0)
		movsq_end = (write_fault ? rdi : rsi) + (rcx * 8ULL);

	src_touch = (fault_addr >= (rsi & ~0xFFFULL) &&
		       fault_addr < rsi + (rcx ? rcx * 8ULL : 8ULL));
	dst_touch = (fault_addr >= (rdi & ~0xFFFULL) &&
		     fault_addr < rdi + (rcx ? rcx * 8ULL : 8ULL));

	serial_print("\n=== [D1.10][PF_STACK_ADJ] ===\n");
	serial_print("[D1.10][REGS] rip=");
	serial_print_hex64(rip);
	serial_print(" rsp=");
	serial_print_hex64(rsp);
	serial_print(" rbp=");
	serial_print_hex64(rbp);
	serial_print(" rax=");
	serial_print_hex64(rax);
	serial_print(" rbx=");
	serial_print_hex64(rbx);
	serial_print(" rcx=");
	serial_print_hex64(rcx);
	serial_print(" rdx=");
	serial_print_hex64(rdx);
	serial_print(" rsi=");
	serial_print_hex64(rsi);
	serial_print(" rdi=");
	serial_print_hex64(rdi);
	serial_print("\n");

	serial_print("[D1.10][PF] cr2=");
	serial_print_hex64(fault_addr);
	serial_print(" err=");
	serial_print_hex64(errcode);
	serial_print(" write=");
	serial_print_hex64(write_fault ? 1 : 0);
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" comm=");
	serial_print(p->comm[0] ? p->comm : "(none)");
	serial_print("\n");

	if (rip >= 0x4422b0ULL && rip <= 0x442320ULL)
	{
		serial_print("[D1.10][REP_MOVSQ] len_qwords=");
		serial_print_hex64(rcx);
		serial_print(" len_bytes=");
		serial_print_hex64(rcx * 8ULL);
		serial_print(" src=");
		serial_print_hex64(rsi);
		serial_print(" dst=");
		serial_print_hex64(rdi);
		serial_print(" span_end=");
		serial_print_hex64(movsq_end);
		serial_print("\n");
	}

	serial_print("[D1.10][TOUCH] cr2_match=");
	if (write_fault)
		serial_print(dst_touch ? "destination" :
			     (src_touch ? "source_read_unlikely" : "unknown"));
	else
		serial_print(src_touch ? "source" :
			     (dst_touch ? "dest_write_unlikely" : "unknown"));
	serial_print(" src_page=");
	serial_print_hex64(rsi & ~0xFFFULL);
	serial_print(" dst_page=");
	serial_print_hex64(rdi & ~0xFFFULL);
	serial_print("\n");

	serial_print("[D1.10][STACK] base=");
	serial_print_hex64((uint64_t)stack_lo);
	serial_print(" top=");
	serial_print_hex64((uint64_t)stack_hi);
	serial_print(" guard_below=");
	serial_print_hex64((uint64_t)guard_lo);
	serial_print(" pages=");
	serial_print_hex64((uint64_t)(p->stack_size / PAGE_SIZE_4KB));
	serial_print(" rsp_free_to_base=");
	serial_print_hex64(rsp > stack_lo ? rsp - stack_lo : 0);
	serial_print(" heap_end=");
	serial_print_hex64(p->heap_end);
	serial_print(" mmap_base=");
	serial_print_hex64(p->mmap_base);
	serial_print("\n");

	serial_print("[D1.10][VMA] stack=[");
	serial_print_hex64((uint64_t)stack_lo);
	serial_print(",");
	serial_print_hex64((uint64_t)stack_hi);
	serial_print(")\n");

	prev_mmap = NULL;
	next_mmap = NULL;
	prev_end = 0;
	next_start = ~0ULL;
	for (r = p->mmap_list; r != NULL; r = r->next)
	{
		uint64_t start = (uint64_t)(uintptr_t)r->addr;
		uint64_t end = start + (uint64_t)r->length;

		if (end <= fault_addr && end > prev_end)
		{
			prev_end = end;
			prev_mmap = r;
		}
		if (start > fault_addr && start < next_start)
		{
			next_start = start;
			next_mmap = r;
		}
		serial_print("[D1.10][VMA] mmap=[");
		serial_print_hex64(start);
		serial_print(",");
		serial_print_hex64(end);
		serial_print(") prot=");
		serial_print_hex64((uint64_t)r->prot);
		serial_print("\n");
	}

	if (p->heap_end > p->heap_start)
	{
		serial_print("[D1.10][VMA] heap=[");
		serial_print_hex64(p->heap_start);
		serial_print(",");
		serial_print_hex64(p->heap_end);
		serial_print(")\n");
	}

	if (prev_mmap)
	{
		serial_print("[D1.10][VMA] prev_mmap_end=");
		serial_print_hex64(prev_end);
		serial_print("\n");
	}
	else
	{
		serial_print("[D1.10][VMA] prev_mmap_end=none gap_from_prev=");
		serial_print_hex64(fault_addr - USER_MMAP_END);
		serial_print("\n");
	}

	if (next_mmap)
	{
		serial_print("[D1.10][VMA] next_mmap_start=");
		serial_print_hex64(next_start);
		serial_print("\n");
	}
	else
	{
		serial_print("[D1.10][VMA] next_mmap_start=none gap_to_stack=");
		serial_print_hex64(stack_lo - fault_addr);
		serial_print("\n");
	}

	serial_print("[D1.10][VMA] guard_gap=[");
	serial_print_hex64((uint64_t)guard_lo);
	serial_print(",");
	serial_print_hex64((uint64_t)stack_lo);
	serial_print(") unmapped\n");
	serial_print("=== [D1.10][PF_STACK_ADJ] end ===\n\n");
}

static void pf_d114_memmove_fault_diag(uint64_t *frame, uint64_t fault_addr,
				       uint64_t errcode, process_t *p)
{
	uint64_t rip;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;

	if (!frame || !p || !(errcode & 4))
		return;

	rip = frame[2];
	if (rip < 0x4422B0ULL || rip > 0x442320ULL)
		return;

	rcx = frame[-2];
	rdx = frame[-3];
	rsi = frame[-6];
	rdi = frame[-7];
	d1_13_malloc_pf_diag(p, fault_addr, rip, rdi, rsi, rdx, rcx);
}

static void pf_audit_classify(uint64_t *stack, uint64_t fault_addr, uint64_t errcode)
{
#if !DEBUG_PAGE_FAULTS
	(void)stack;
	(void)fault_addr;
	(void)errcode;
	return;
#else
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
#endif /* DEBUG_PAGE_FAULTS */
}

/* Try SIGSEGV handler delivery; otherwise terminate (noreturn). */
static void pf_user_segv(process_t *p, uint64_t *stack, uint64_t fault_addr,
			 uint64_t errcode)
{
	if (signals_deliver_from_irq_frame(p, SIGSEGV, stack, fault_addr))
		return;

	ktm_probe_diag_pf(p, fault_addr, stack ? stack[2] : 0);

	if (!p)
		panic("[PF] userspace fault without process");

	serial_print("[PF] userspace segv pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" addr=");
	serial_print_hex64(fault_addr);
	serial_print(" err=");
	serial_print_hex64(errcode);
	serial_print(" handler=");
	serial_print_hex64((uint64_t)(uintptr_t)p->signal_handlers[SIGSEGV]);
	serial_print(" proc_mask=");
	serial_print_hex32(p->signal_mask);
	serial_print(" ignored=");
	serial_print_hex32(p->signal_ignored);
	serial_print(" (no handler)\n");

	(void)send_signal(p->task.pid, SIGSEGV);
	process_exit(128 + SIGSEGV);
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
#if DEBUG_D1_DIAG
	pf_d110_stack_adjacent_diag(stack, fault_addr, errcode,
				    process_get_current());
#endif
	pf_d114_memmove_fault_diag(stack, fault_addr, errcode,
				   process_get_current());

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
				pf_user_segv(current, stack, fault_addr, errcode);
			}
			return;
		}

		current = process_get_current();
		if (!current || !current->page_directory)
			return;

		if (pf_mmap_region_for(current, fault_addr) != NULL) {
			pf_user_segv(current, stack, fault_addr, errcode);
			return;
		}

		if (!pf_addr_in_heap(current, fault_addr) &&
		    !pf_addr_in_stack(current, fault_addr)) {
			pf_user_segv(current, stack, fault_addr, errcode);
			return;
		}

		/* Allocate physical frame using PMM */
		uintptr_t phys_addr = pmm_alloc_frame();

		if (phys_addr == 0) {
			if (current) {
				pf_user_segv(current, stack, fault_addr, errcode);
			}
			return;
		}

		/*
		 * Heap/stack only — mmap PTEs come from sys_mmap / sys_mprotect.
		 */
		uint64_t flags = PAGE_USER | PAGE_RW;
		if (insn_fetch)
			flags |= PAGE_EXEC;

		/* Map page in process page directory */
		uint64_t vaddr_aligned = fault_addr & ~0xFFF;

		if (map_page_in_directory(current->page_directory, vaddr_aligned,
					   phys_addr, flags) != 0) {
			pmm_free_frame(phys_addr);
			if (current) {
				pf_user_segv(current, stack, fault_addr, errcode);
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
			pf_user_segv(current, stack, fault_addr, errcode);
		}
		return;
	}

	if (user) {
		current = process_get_current();
		if (current)
			pf_user_segv(current, stack, fault_addr, errcode);
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
#if !DEBUG_PAGE_FAULTS
	(void)stack;
	return;
#else
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
#endif /* DEBUG_PAGE_FAULTS */
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
