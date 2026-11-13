/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: diag.c
 * Description: MM diagnostics over KTM: page-fault audit and history, the
 *              fatal-SIGSEGV dump, kernel-fault reports and fault injection.
 *              Policy code in mm/ calls these hooks and never logs itself.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/logging.h>
#include <ir0/mm_diag.h>
#include <ir0/oops.h>
#include <ir0/fault.h>
#include <ir0/arch_pf_debug.h>
#include <ir0/ktm/deferred.h>
#include <ir0/ktm/event.h>
#include <ir0/ktm/fault.h>
#include <ir0/ktm/user_canary.h>
#include <ktm.h>
#include <ktm_probe_diag.h>
#include <ir0/paging.h>
#include "page_fault_internal.h"
#include "user_va.h"

int mm_diag_fault_hit(const char *name)
{
	return KTM_FAULT_HIT(name);
}

static void pf_diag_audit(const struct page_fault_info *info)

{
#if !DEBUG_PAGE_FAULTS
	(void)info;
	return;
#else
	process_t *current = process_get_current();
	uint64_t fault_addr;
	int not_present;
	uint64_t fault_ip;
	uint64_t fault_code_seg;
	uint64_t fault_sp;
	uint64_t *pte = NULL;
	uint64_t pte_flags = 0;
	int mapped = 0;
	int in_vma = 0;
	int in_userspace_range = 0;

	if (!info)
		return;

	fault_addr = (uint64_t)info->address;
	not_present = !info->present;
	fault_ip = (uint64_t)info->ip;
	fault_code_seg = info->code_seg;
	fault_sp = (uint64_t)info->sp;

	if (current && process_pgd(current))
	{
		mapped = is_page_mapped_in_directory(process_pgd(current),
						   fault_addr, &pte_flags);
		pte = paging_get_pte(process_pgd(current),
				     (uintptr_t)(fault_addr & ~0xFFFULL));
	}

	in_vma = pf_addr_in_heap(current, fault_addr) ||
		 pf_addr_in_stack(current, fault_addr) ||
		 (find_vma(current, fault_addr) != NULL);
	in_userspace_range = mm_user_va_ok((uintptr_t)fault_addr, 1);

	log_debug_fmt("PF", "[PF_AUDIT][FAULT] addr=%llx present=%llx write=%llx user=%llx reserved=%llx insn_fetch=%llx rip=%llx cs=%llx rsp=%llx mode=%s pid=%x comm=%s", (unsigned long long)(fault_addr), (unsigned long long)(info->present ? 1 : 0), (unsigned long long)(info->write ? 1 : 0), (unsigned long long)(info->user ? 1 : 0), (unsigned long long)(info->reserved ? 1 : 0), (unsigned long long)(info->exec ? 1 : 0), (unsigned long long)(fault_ip), (unsigned long long)(fault_code_seg), (unsigned long long)(fault_sp), info->user ? "user" : "kernel", (unsigned)(current ? (uint32_t)current->task.pid : 0), current ? current->comm : "(none)");

	log_debug_fmt("PF", "[PF_AUDIT][VMA] in_allowed_vma=%llx in_heap=%llx in_stack=%llx in_mmap=%llx pte_present=%llx pte_user=%llx pte_rw=%llx pte_nx=%llx", (unsigned long long)(in_vma ? 1 : 0), (unsigned long long)(pf_addr_in_heap(current, fault_addr) ? 1 : 0), (unsigned long long)(pf_addr_in_stack(current, fault_addr) ? 1 : 0), (unsigned long long)(find_vma(current, fault_addr) != NULL ? 1 : 0), (unsigned long long)((mapped > 0 && pte && mm_pte_present(*pte)) ? 1 : 0), (unsigned long long)(pte_flags & PAGE_USER ? 1 : 0), (unsigned long long)(pte_flags & PAGE_RW ? 1 : 0), (unsigned long long)(pte && !mm_pte_executable(*pte) ? 1 : 0));

	if (!info->user && in_userspace_range)
		log_debug("PF", "CLASSIFY KERNEL_DEREF_USERPTR addr_in_userspace=1");

	if (info->user && not_present && in_vma && mapped <= 0)
		log_debug("PF", "CLASSIFY PF_ADDR_IN_VMA_NOT_MAPPED");
	else if (info->user && not_present && !in_vma)
		log_debug("PF", "CLASSIFY PF_ADDR_NOT_IN_VMA");
	else if (info->user && not_present && in_vma)
		log_debug("PF", "CLASSIFY USER_PF_SHOULD_BE_HANDLED");

	if (!info->user && fault_ip != 0)
		log_debug_fmt("PF", "CLASSIFY kernel_fault_rip=%llx", (unsigned long long)(fault_ip));
#endif /* DEBUG_PAGE_FAULTS */
}

void pf_diag_fault_entry(const struct page_fault_info *info, uint64_t *stack)
{
	process_t *current = process_get_current();
	uint64_t fault_addr = (uint64_t)info->address;

	pf_diag_audit(info);
	pf_debug_stack_adjacent(stack, fault_addr, info, current);
	pf_debug_memmove_fault(stack, fault_addr, info, current);

	/*
	 * Recorded, not emitted: this runs on every demand-paging fault, so
	 * formatting or event emission here would dominate the path it
	 * measures. The rows only reach the ring on a dump, which is what
	 * turns a later crash into a fault history instead of a single
	 * address.
	 */
	ktm_deferred_record(KTM_DEFERRED_PAGE_FAULT,
			    current ? (uint32_t)current->task.pid : 0,
			    fault_addr,
			    (uint64_t)(unsigned)((info->present ? 0U : 1U) |
						(info->write ? 2U : 0U) |
						(info->user ? 4U : 0U) |
						(info->exec ? 8U : 0U)),
			    (uint64_t)info->ip);
}

void pf_diag_user_segv(process_t *p, uint64_t fault_addr,
		       const struct page_fault_info *info)
{
	ktm_probe_diag_pf(p, fault_addr, info ? info->ip : 0);

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

	ktm_user_canary_check(process_pgd(p), (uint64_t)USER_STACK_TOP,
			      (uint32_t)p->task.pid, "segv");
	ktm_event_ring_dump(48, (1U << KTM_SUBSYS_IPC) |
			    (1U << KTM_SUBSYS_SCHED) |
			    (1U << KTM_SUBSYS_MM));

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
			"sigframe_sp=%llx mask_saved=%x "
			"saved_ctx=%x\n",
			(unsigned)((uint32_t)p->task.pid),
			(unsigned long long)task_get_arg0(&p->task),
			(unsigned long long)task_get_arg1(&p->task),
			(unsigned long long)(info ? info->sp : 0ULL),
			(unsigned long long)p->signal_frame_sp,
			(unsigned)(p->signal_mask_saved_valid ? 1U : 0U),
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
		struct mmap_region *mr = find_vma(p, fault_addr);

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

void pf_diag_kernel_uaccess(const struct ir0_fault *fault, unsigned int pid,
			    const char *comm)
{
	/*
	 * USER_FAULT_FRAME is logged at INFO so session smokes see the fault
	 * site, not only CONSOLE_SESSION_SEGV; they also grep the CLASSIFY tag.
	 */
	ir0_log_user_fault(fault, pid, comm);
	log_info_fmt("PF",
		     "CLASSIFY KERNEL_UACCESS_FAULT addr=%llx write=%x "
		     "pid=%x ip=%llx",
		     (unsigned long long)fault->addr,
		     (unsigned)(fault->write ? 1 : 0), pid,
		     (unsigned long long)fault->ip);
}

void pf_diag_kernel_fault(const struct ir0_fault *fault,
			  const struct page_fault_info *info, unsigned int pid)
{
	uint64_t fault_addr = (uint64_t)fault->addr;

	log_error_fmt("PF",
		      "kernel_pf addr=%llx write=%x user=%x present=%x ip=%llx "
		      "sp=%llx pid=%x",
		      (unsigned long long)fault_addr,
		      (unsigned)(fault->write ? 1 : 0),
		      (unsigned)(info->user ? 1 : 0),
		      (unsigned)(fault->present ? 1 : 0),
		      (unsigned long long)fault->ip,
		      (unsigned long long)fault->sp, pid);
	if (fault_addr >= (uint64_t)(intptr_t)-4095)
		log_error_fmt("PF", "addr looks like ERR_PTR(-errno) errno=%llx",
			      (unsigned long long)(-(intptr_t)fault_addr));
}
