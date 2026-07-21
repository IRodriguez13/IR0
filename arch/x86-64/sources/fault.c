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
#include <ir0/cpu.h>
#include <ir0/process.h>
#include <string.h>
#include <ir0/pmm.h>
#include <ir0/signals.h>
#include <arch/common/arch_portable.h>
#include <ir0/copy_user.h>
#include <ir0/ktm/klog.h>
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

	klog_debug_fmt("KERN", "\n=== [D1.10][PF_STACK_ADJ] ===\n[D1.10][REGS] rip=%llx rsp=%llx rbp=%llx rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx", (unsigned long long)(rip), (unsigned long long)(rsp), (unsigned long long)(rbp), (unsigned long long)(rax), (unsigned long long)(rbx), (unsigned long long)(rcx), (unsigned long long)(rdx), (unsigned long long)(rsi), (unsigned long long)(rdi));

	klog_debug_fmt("KERN", "[D1.10][PF] cr2=%llx err=%llx write=%llx pid=%x comm=%s", (unsigned long long)(fault_addr), (unsigned long long)(errcode), (unsigned long long)(write_fault ? 1 : 0), (unsigned)((uint32_t)p->task.pid), p->comm[0] ? p->comm : "(none)");

	if (rip >= 0x4422b0ULL && rip <= 0x442320ULL)
	{
		klog_debug_fmt("KERN", "[D1.10][REP_MOVSQ] len_qwords=%llx len_bytes=%llx src=%llx dst=%llx span_end=%llx", (unsigned long long)(rcx), (unsigned long long)(rcx * 8ULL), (unsigned long long)(rsi), (unsigned long long)(rdi), (unsigned long long)(movsq_end));
	}

	klog_debug_fmt("KERN",
		       "[D1.10][TOUCH] cr2_match=%s src_page=%llx dst_page=%llx",
		       write_fault
			   ? (dst_touch ? "destination"
					: (src_touch ? "source_read_unlikely" : "unknown"))
			   : (src_touch ? "source"
					: (dst_touch ? "dest_write_unlikely" : "unknown")),
		       (unsigned long long)(rsi & ~0xFFFULL),
		       (unsigned long long)(rdi & ~0xFFFULL));

	klog_debug_fmt("KERN", "[D1.10][STACK] base=%llx top=%llx guard_below=%llx pages=%llx rsp_free_to_base=%llx heap_end=%llx mmap_base=%llx", (unsigned long long)((uint64_t)stack_lo), (unsigned long long)((uint64_t)stack_hi), (unsigned long long)((uint64_t)guard_lo), (unsigned long long)((uint64_t)(p->stack_size / PAGE_SIZE_4KB)), (unsigned long long)(rsp > stack_lo ? rsp - stack_lo : 0), (unsigned long long)(p->heap_end), (unsigned long long)(p->mmap_base));

	klog_debug_fmt("KERN", "[D1.10][VMA] stack=[%llx,%llx)\n", (unsigned long long)((uint64_t)stack_lo), (unsigned long long)((uint64_t)stack_hi));

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
		klog_debug_fmt("KERN", "[D1.10][VMA] mmap=[%llx,%llx) prot=%llx", (unsigned long long)(start), (unsigned long long)(end), (unsigned long long)((uint64_t)r->prot));
	}

	if (p->heap_end > p->heap_start)
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] heap=[%llx,%llx)\n", (unsigned long long)(p->heap_start), (unsigned long long)(p->heap_end));
	}

	if (prev_mmap)
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] prev_mmap_end=%llx", (unsigned long long)(prev_end));
	}
	else
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] prev_mmap_end=none gap_from_prev=%llx", (unsigned long long)(fault_addr - USER_MMAP_END));
	}

	if (next_mmap)
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] next_mmap_start=%llx", (unsigned long long)(next_start));
	}
	else
	{
		klog_debug_fmt("KERN", "[D1.10][VMA] next_mmap_start=none gap_to_stack=%llx", (unsigned long long)(stack_lo - fault_addr));
	}

	klog_debug_fmt("KERN", "[D1.10][VMA] guard_gap=[%llx,%llx) unmapped\n=== [D1.10][PF_STACK_ADJ] end ===\n\n", (unsigned long long)((uint64_t)guard_lo), (unsigned long long)((uint64_t)stack_lo));
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

	klog_debug_fmt("PF", "[PF_AUDIT][FAULT] cr2=%llx err=%llx present=%llx write=%llx user=%llx reserved=%llx insn_fetch=%llx rip=%llx cs=%llx rsp=%llx mode=%s pid=%x comm=%s", (unsigned long long)(fault_addr), (unsigned long long)(errcode), (unsigned long long)(not_present ? 0 : 1), (unsigned long long)(write ? 1 : 0), (unsigned long long)(user ? 1 : 0), (unsigned long long)(reserved ? 1 : 0), (unsigned long long)(insn_fetch ? 1 : 0), (unsigned long long)(fault_rip), (unsigned long long)(fault_cs), (unsigned long long)(fault_rsp), user ? "user" : "kernel", (unsigned)(current ? (uint32_t)current->task.pid : 0), current ? current->comm : "(none)");

	klog_debug_fmt("PF", "[PF_AUDIT][VMA] in_allowed_vma=%llx in_heap=%llx in_stack=%llx in_mmap=%llx pte_present=%llx pte_user=%llx pte_rw=%llx pte_nx=%llx", (unsigned long long)(in_vma ? 1 : 0), (unsigned long long)(pf_addr_in_heap(current, fault_addr) ? 1 : 0), (unsigned long long)(pf_addr_in_stack(current, fault_addr) ? 1 : 0), (unsigned long long)(pf_addr_in_mmap(current, fault_addr) ? 1 : 0), (unsigned long long)((mapped > 0 && pte && (*pte & PAGE_PRESENT)) ? 1 : 0), (unsigned long long)(pte_flags & PAGE_USER ? 1 : 0), (unsigned long long)(pte_flags & PAGE_RW ? 1 : 0), (unsigned long long)(pte && (*pte & PAGE_NX) ? 1 : 0));

	if (!user && in_userspace_range)
	{
		klog_debug("PF", "CLASSIFY KERNEL_DEREF_USERPTR cr2_in_userspace=1");
	}

	if (user && not_present && in_vma && mapped <= 0)
	{
		klog_debug("PF", "CLASSIFY PF_ADDR_IN_VMA_NOT_MAPPED");
	}
	else if (user && not_present && !in_vma)
	{
		klog_debug("PF", "CLASSIFY PF_ADDR_NOT_IN_VMA");
	}
	else if (user && not_present && in_vma)
	{
		klog_debug("PF", "CLASSIFY USER_PF_SHOULD_BE_HANDLED");
	}

	if (!user && fault_rip != 0)
	{
		klog_debug_fmt("PF", "CLASSIFY kernel_fault_rip=%llx", (unsigned long long)(fault_rip));
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

	klog_debug_fmt("PF", "[PF] userspace segv pid=%x addr=%llx err=%llx handler=%llx proc_mask=%x ignored=%x (no handler)\n", (unsigned)((uint32_t)p->task.pid), (unsigned long long)(fault_addr), (unsigned long long)(errcode), (unsigned long long)((uint64_t)(uintptr_t)p->signal_handlers[SIGSEGV]), (unsigned)(p->signal_mask), (unsigned)(p->signal_ignored));

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
		 * Zero via physical identity map — never switch CR3 and memset a
		 * user VA from CPL0 (that #PF looks like kernel fault → panic).
		 */
		memset((void *)(uintptr_t)phys_addr, 0, 0x1000);

		return;
	}

	/* Handle write protection fault (page present but not writable) */
	if (user && !not_present && write) {
		uint64_t *pte;
		uint64_t entry;
		uintptr_t old_phys;
		uintptr_t new_phys;
		uint64_t vaddr_aligned;
		uint64_t map_flags;

		current = process_get_current();
		if (!current || !current->page_directory) {
			return;
		}

		vaddr_aligned = fault_addr & ~0xFFFUL;
		pte = paging_get_pte(current->page_directory, vaddr_aligned);
		if (!pte || !(*pte & PAGE_PRESENT) || !(*pte & PAGE_USER) ||
		    !(*pte & PAGE_COW) || (*pte & PAGE_RW)) {
			pf_user_segv(current, stack, fault_addr, errcode);
			return;
		}

		entry = *pte;
		old_phys = (uintptr_t)(entry & PAGE_PTE_PFN_MASK);

		/* Last reference: just re-enable write, clear COW. */
		if (pmm_frame_refcount(old_phys) <= 1) {
			*pte = (entry | PAGE_RW) & ~PAGE_COW;
			tlb_invalidate_page((uintptr_t)vaddr_aligned);
			return;
		}

		new_phys = pmm_alloc_frame();
		if (!new_phys) {
			pf_user_segv(current, stack, fault_addr, errcode);
			return;
		}

		memcpy((void *)new_phys, (void *)old_phys, 0x1000);

		map_flags = (entry & 0xFFF) | PAGE_USER | PAGE_RW;
		map_flags &= ~(PAGE_COW | PAGE_GLOBAL);
		if (!(entry & PAGE_NX))
			map_flags |= PAGE_EXEC;

		if (map_page_in_directory(current->page_directory, vaddr_aligned,
					  new_phys, map_flags) != 0) {
			pmm_free_frame(new_phys);
			pf_user_segv(current, stack, fault_addr, errcode);
			return;
		}

		pmm_frame_put(old_phys);
		tlb_invalidate_page((uintptr_t)vaddr_aligned);
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

	/*
	 * Supervisor access to a userspace VA (bad uaccess / CR3 mismatch).
	 * Never panic the whole machine — tear down the offending task.
	 */
	if (fault_addr >= PF_USER_SPACE_START && fault_addr <= PF_USER_SPACE_END)
	{
		current = process_get_current();

		klog_debug_fmt("PF", "[PF] kernel_uaccess_fault addr=%llx err=%llx pid=%x", (unsigned long long)(fault_addr), (unsigned long long)(errcode), (unsigned)(current ? (uint32_t)current->task.pid : 0));
		if (current && current->mode == USER_MODE)
		{
			(void)send_signal(current->task.pid, SIGSEGV);
			process_exit(128 + SIGSEGV);
		}
		panic("Unhandled kernel page fault (uaccess, no user task)");
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

	klog_debug_fmt("GPF", "err=%llx rip=%llx cs=%llx rsp=%llx ss=%llx rflags=%llx mode=%s pid=%x comm=%s cr3=%llx", (unsigned long long)(errcode), (unsigned long long)(fault_rip), (unsigned long long)(fault_cs), (unsigned long long)(fault_rsp), (unsigned long long)(fault_ss), (unsigned long long)(fault_rflags), user ? "user" : "kernel", (unsigned)(current ? (uint32_t)current->task.pid : 0), current ? current->comm : "(none)", (unsigned long long)(get_current_page_directory()));

	klog_debug_fmt("GPF", "iretq_ckpt rip=%llx cs=%llx rsp=%llx", (unsigned long long)(ckpt_rip), (unsigned long long)(ckpt_cs), (unsigned long long)(ckpt_rsp));

	if (!user)
	{
		klog_debug("GPF", "CLASSIFY GPF_IN_KERNEL_BEFORE_IRET");
		if (fault_rip == ckpt_rip ||
		    (fault_rip >= 0x160000ULL && fault_rip <= 0x170000ULL))
		{
			klog_debug("GPF",
				   "CLASSIFY GPF_DURING_IRETQ note=rip_near_switch_to_user");
		}
	}
	else
	{
		klog_debug("GPF", "CLASSIFY GPF_IN_USERSPACE");
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
