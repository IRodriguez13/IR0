/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault_diag.c
 * Description: Page-fault diagnostics (PF_AUDIT and the fatal-SIGSEGV dump),
 *              kept out of the fault policy in page_fault*.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <config.h>
#include <ir0/logging.h>
#include <ir0/mm_diag.h>
#include <mm/paging.h>
#include "page_fault_internal.h"

void pf_diag_audit(const struct page_fault_info *info)
{
#if !DEBUG_PAGE_FAULTS
	(void)info;
	return;
#else
	process_t *current = process_get_current();
	uint64_t fault_addr;
	int not_present;
	uint64_t fault_rip;
	uint64_t fault_cs;
	uint64_t fault_rsp;
	uint64_t *pte = NULL;
	uint64_t pte_flags = 0;
	int mapped = 0;
	int in_vma = 0;
	int in_userspace_range = 0;

	if (!info)
		return;

	fault_addr = (uint64_t)info->address;
	not_present = !info->present;
	fault_rip = (uint64_t)info->ip;
	fault_cs = info->code_seg;
	fault_rsp = (uint64_t)info->sp;

	if (current && process_pgd(current))
	{
		mapped = is_page_mapped_in_directory(process_pgd(current),
						   fault_addr, &pte_flags);
		pte = paging_get_pte(process_pgd(current),
				     (uintptr_t)(fault_addr & ~0xFFFULL));
	}

	in_vma = pf_addr_in_heap(current, fault_addr) ||
		 pf_addr_in_stack(current, fault_addr) ||
		 (pf_mmap_region_for(current, fault_addr) != NULL);
	in_userspace_range = mm_user_va_ok((uintptr_t)fault_addr, 1);

	log_debug_fmt("PF", "[PF_AUDIT][FAULT] addr=%llx present=%llx write=%llx user=%llx reserved=%llx insn_fetch=%llx rip=%llx cs=%llx rsp=%llx mode=%s pid=%x comm=%s", (unsigned long long)(fault_addr), (unsigned long long)(info->present ? 1 : 0), (unsigned long long)(info->write ? 1 : 0), (unsigned long long)(info->user ? 1 : 0), (unsigned long long)(info->reserved ? 1 : 0), (unsigned long long)(info->exec ? 1 : 0), (unsigned long long)(fault_rip), (unsigned long long)(fault_cs), (unsigned long long)(fault_rsp), info->user ? "user" : "kernel", (unsigned)(current ? (uint32_t)current->task.pid : 0), current ? current->comm : "(none)");

	log_debug_fmt("PF", "[PF_AUDIT][VMA] in_allowed_vma=%llx in_heap=%llx in_stack=%llx in_mmap=%llx pte_present=%llx pte_user=%llx pte_rw=%llx pte_nx=%llx", (unsigned long long)(in_vma ? 1 : 0), (unsigned long long)(pf_addr_in_heap(current, fault_addr) ? 1 : 0), (unsigned long long)(pf_addr_in_stack(current, fault_addr) ? 1 : 0), (unsigned long long)(pf_mmap_region_for(current, fault_addr) != NULL ? 1 : 0), (unsigned long long)((mapped > 0 && pte && mm_pte_present(*pte)) ? 1 : 0), (unsigned long long)(pte_flags & PAGE_USER ? 1 : 0), (unsigned long long)(pte_flags & PAGE_RW ? 1 : 0), (unsigned long long)(pte && !mm_pte_executable(*pte) ? 1 : 0));

	if (!info->user && in_userspace_range)
		log_debug("PF", "CLASSIFY KERNEL_DEREF_USERPTR addr_in_userspace=1");

	if (info->user && not_present && in_vma && mapped <= 0)
		log_debug("PF", "CLASSIFY PF_ADDR_IN_VMA_NOT_MAPPED");
	else if (info->user && not_present && !in_vma)
		log_debug("PF", "CLASSIFY PF_ADDR_NOT_IN_VMA");
	else if (info->user && not_present && in_vma)
		log_debug("PF", "CLASSIFY USER_PF_SHOULD_BE_HANDLED");

	if (!info->user && fault_rip != 0)
		log_debug_fmt("PF", "CLASSIFY kernel_fault_rip=%llx", (unsigned long long)(fault_rip));
#endif /* DEBUG_PAGE_FAULTS */
}

void pf_diag_user_segv(process_t *p, uint64_t fault_addr,
		       const struct page_fault_info *info)
{
	mm_diag_probe_user_fault(p, fault_addr, info ? info->ip : 0);

	/*
	 * Unhandled segv: the process is already dead, so the ring is worth
	 * more on serial than in memory. Dumping here means every smoke gets
	 * the history leading to a crash without wiring the ioctl itself —
	 * the host-side harness only reads serial and cannot ask for it.
	 * Reached only when no SIGSEGV handler took the fault above.
	 *
	 * MM belongs in the mask: the deferred page-fault rows are the fault
	 * history for this crash, and filtering to IPC|SCHED dropped exactly
	 * them, leaving scheduler churn and no faults.
	 */
	/*
	 * Name the failure when the address sits in the page just above the
	 * stack: "segv at 7ffff000" reads as a wild pointer, but it is the
	 * first address past USER_STACK_TOP and means something walked off the
	 * top of the initial argv/envp image.
	 */
	if (fault_addr >= (uint64_t)USER_STACK_TOP &&
	    fault_addr < (uint64_t)USER_STACK_TOP + PAGE_SIZE_4KB)
	{
		uint64_t sp_words[4] = { 0, 0, 0, 0 };

		log_info_fmt("PF",
				"[PF] STACK_TOP_OVERRUN pid=%x addr=%llx rip=%llx write=%llx off=%llx\n",
				(unsigned)((uint32_t)p->task.pid),
				(unsigned long long)fault_addr,
				(unsigned long long)(info ? (unsigned long long)info->ip : 0ULL),
				(unsigned long long)(info && info->write ? 1 : 0),
				(unsigned long long)(fault_addr - (uint64_t)USER_STACK_TOP));

		/*
		 * The fault RIP lands inside the libc block copy, so it names
		 * memcpy and not whoever asked for the bad length. Snapshot the
		 * top of the user stack instead: musl's x86-64 memcpy pushes
		 * nothing, so the first word is the caller's return address.
		 *
		 * That last part is a SysV assumption. On link-register ISAs the
		 * caller lives in a register and this only shows stack words;
		 * recovering the caller there needs an arch facade for the GPR
		 * set, which does not exist yet.
		 */
		if (info && info->sp &&
		    copy_from_user_mm(process_pgd(p),
						       (uintptr_t)info->sp,
						       sp_words,
						       sizeof(sp_words)) == 0)
			log_info_fmt("PF",
					"[PF] STACK_TOP_OVERRUN usp=%llx w0=%llx w1=%llx w2=%llx w3=%llx\n",
					(unsigned long long)info->sp,
					(unsigned long long)sp_words[0],
					(unsigned long long)sp_words[1],
					(unsigned long long)sp_words[2],
					(unsigned long long)sp_words[3]);
	}

	mm_diag_dump_user_segv(p, (uint64_t)USER_STACK_TOP);

	/*
	 * NOTICE (not DEBUG): smoke-mm-cow-lazy greps this on serial; default
	 * klog level drops DEBUG.
	 */
	log_info_fmt("PF",
			"[PF] userspace segv pid=%x addr=%llx write=%llx present=%llx rip=%llx user=%llx handler=%llx proc_mask=%x ignored=%x (no handler)\n",
			(unsigned)((uint32_t)p->task.pid),
			(unsigned long long)fault_addr,
			(unsigned long long)(info && info->write ? 1 : 0),
			(unsigned long long)(info && info->present ? 1 : 0),
			(unsigned long long)(info ? (unsigned long long)info->ip : 0ULL),
			(unsigned long long)(info && info->user ? 1 : 0),
			(unsigned long long)((uint64_t)(uintptr_t)p->signal_handlers[SIGSEGV]),
			(unsigned)(p->signal_mask),
			(unsigned)(p->signal_ignored));
	log_info_fmt("PF",
			"[PF] segv diag pid=%x arg0=%llx arg1=%llx sp=%llx "
			"sigframe_sp=%llx mask_saved=%x enter_pend=%x "
			"saved_ctx=%x\n",
			(unsigned)((uint32_t)p->task.pid),
			(unsigned long long)task_get_arg0(&p->task),
			(unsigned long long)task_get_arg1(&p->task),
			(unsigned long long)(info ? info->sp : 0ULL),
			(unsigned long long)p->signal_frame_sp,
			(unsigned)(p->signal_mask_saved_valid ? 1U : 0U),
			(unsigned)(process_signal_enter_pending(p) ? 1U : 0U),
			(unsigned)(process_saved_context_present(p) ? 1U : 0U));

	/*
	 * A user fault with P=1 is a permission violation, not a missing page,
	 * so the PTE flags say which bit denied it (U/S clear => a supervisor
	 * mapping is covering user VA). Without this the log could not tell a
	 * leftover kernel mapping from an honest unmapped access.
	 */
	{
		uint64_t pte_flags = 0;
		int mapped = is_page_mapped_in_directory(process_pgd(p),
							 (uint64_t)fault_addr,
							 &pte_flags);

		log_info_fmt("PF",
				"[PF] segv pte pid=%x addr=%llx mapped=%llx flags=%llx user=%llx rw=%llx\n",
				(unsigned)((uint32_t)p->task.pid),
				(unsigned long long)fault_addr,
				(unsigned long long)(uint64_t)mapped,
				(unsigned long long)pte_flags,
				(unsigned long long)((pte_flags & PAGE_USER) ? 1ULL : 0ULL),
				(unsigned long long)((pte_flags & PAGE_RW) ? 1ULL : 0ULL));
	}

	/* Locate the fault against the address space: heap vs mmap vs neither. */
	{
		struct mmap_region *mr = pf_mmap_region_for(p, fault_addr);

		/*
		 * Split across two records: klog_notice_fmt drops arguments
		 * past the eighth, which silently corrupted the tail fields.
		 */
		log_info_fmt("PF",
				"[PF] segv vma pid=%x addr=%llx heap=[%llx,%llx) stack=%llx\n",
				(unsigned)((uint32_t)p->task.pid),
				(unsigned long long)fault_addr,
				(unsigned long long)(uint64_t)process_heap_start(p),
				(unsigned long long)(uint64_t)process_heap_end(p),
				(unsigned long long)(pf_addr_in_stack(p, fault_addr) ? 1ULL : 0ULL));
		if (mr)
			log_info_fmt("PF",
					"[PF] segv vma rgn pid=%x vma=%llx len=%llx prot=%llx flags=%llx hint=%llx\n",
					(unsigned)((uint32_t)p->task.pid),
					(unsigned long long)(uint64_t)(uintptr_t)mr->addr,
					(unsigned long long)(uint64_t)mr->length,
					(unsigned long long)(uint64_t)mr->prot,
					(unsigned long long)(uint64_t)mr->flags,
					(unsigned long long)(uint64_t)(uintptr_t)mr->hint_addr);
	}
}
