/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault_cow.c
 * Description: User write to a present read-only page: copy-on-write break or SIGSEGV.
 */

#include <ir0/cpu.h>
#include <ir0/tlb_sync.h>
#include <ir0/process.h>
#include <ir0/mm_alloc.h>
#include <ir0/pmm.h>
#include <ir0/oom.h>
#include <ir0/logging.h>
#include <ir0/paging.h>
#include "page_fault_internal.h"

void pf_user_write_present(const struct page_fault_info *info,
			   uint64_t *stack, uint64_t fault_addr)
{
	process_t *current;
	int insn_fetch = info->exec;

	uint64_t *pte;
	uint64_t entry;
	uintptr_t old_phys;
	uintptr_t new_phys;
	uint64_t vaddr_aligned;
	uint64_t map_flags;
	unsigned long irq_flags;
	unsigned pinned = 0;

	current = process_get_current();
	if (!current || !process_pgd(current))
		return;

	vaddr_aligned = fault_addr & ~0xFFFUL;
	/*
	 * IRQ-off + optional PMM pin: sibling pipe-stage COW must not
	 * free the shared frame under memcpy (ash pipelines).
	 */
	irq_flags = irq_save();
	/*
	 * Write into an unsplit 2 MiB block: paging_get_pte returns NULL
	 * (logged as pte=0). Split first; COW still requires USER|COW.
	 */
	if (paging_ensure_4k_leaf(process_pgd(current),
				  vaddr_aligned) != 0)
	{
		irq_restore(irq_flags);
		log_info_fmt("PF",
				"[PF] write-fault no-COW pid=%x addr=%llx pte=0\n",
				(unsigned)((uint32_t)current->task.pid),
				(unsigned long long)fault_addr);
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}
	pte = paging_get_pte(process_pgd(current), vaddr_aligned);
	/*
	 * After a 2 MiB block split, slots are pte_none (Linux). Demand-zero
	 * inside heap/stack/mmap — same as !present path above.
	 */
	if (!pte || !pte_present(*pte))
	{
		irq_restore(irq_flags);
		if (pf_addr_may_promote_identity(current, fault_addr))
		{
			uint64_t map_flags = PAGE_USER | PAGE_RW;

			if (insn_fetch)
				map_flags |= PAGE_EXEC;
			pf_demand_zero_page(current, fault_addr, map_flags,
					    stack, info);
			return;
		}
		log_info_fmt("PF",
				"[PF] write-fault no-COW pid=%x addr=%llx pte=0\n",
				(unsigned)((uint32_t)current->task.pid),
				(unsigned long long)fault_addr);
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}
	/*
	 * present+!USER identity leftover: install anon zero
	 * (do_anonymous_page), never memcpy PMM.
	 */
	if (pte && pte_present(*pte) && !pte_user(*pte) &&
	    pf_addr_may_promote_identity(current, fault_addr))
	{
		if (pf_identity_to_anon_zero(current, vaddr_aligned,
					     PAGE_USER | PAGE_RW) != 0)
		{
			irq_restore(irq_flags);
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
		irq_restore(irq_flags);
		return;
	}
	if (!pte || !pte_present(*pte) || !pte_user(*pte) ||
	    !pte_cow(*pte) || pte_write(*pte))
	{
		uint64_t pte_val = pte ? *pte : 0;

		irq_restore(irq_flags);
		log_info_fmt("PF",
				"[PF] write-fault no-COW pid=%x addr=%llx pte=%llx\n",
				(unsigned)((uint32_t)current->task.pid),
				(unsigned long long)fault_addr,
				(unsigned long long)pte_val);
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	entry = *pte;
	old_phys = mm_pte_phys(entry);
	if (pmm_frame_refcount(old_phys) > 0)
	{
		pmm_frame_get(old_phys);
		pinned = 1;
	}

	/*
	 * Always copy on PAGE_COW — never promote the shared frame in
	 * place. A undercounted pmm_frame_get after fork (or a raced
	 * sibling) would otherwise leave the parent mapped RO+COW to
	 * the same phys while the child writes through a RW PTE,
	 * corrupting the parent's .data/.bss (seen as ip/addr=0 SEGV
	 * in multi-fork ash/pipeline smokes).
	 */
	new_phys = mm_alloc_frame(MM_ALLOC_ATOMIC);
	if (!new_phys)
	{
		size_t tot = 0;
		size_t used = 0;
		size_t free_fr = 0;

		if (pinned)
			pmm_frame_put(old_phys);
		irq_restore(irq_flags);
		pmm_stats(&tot, &used, &free_fr);
		log_info_fmt("PF",
				"[PF] COW OOM pid=%x addr=%llx refs=%u used=%u free=%u\n",
				(unsigned)((uint32_t)current->task.pid),
				(unsigned long long)fault_addr,
				(unsigned)pmm_frame_refcount(old_phys),
				(unsigned)used,
				(unsigned)free_fr);
		/*
		 * Return 0: reclaim freed a page, or the killer chose another
		 * task. The faulting instruction runs again. Killing this
		 * task does not return. Anything else fails the fault.
		 */
		if (oom_handle_alloc_failure(current, "cow") == 0)
			return;
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	paging_copy_phys_page(new_phys, old_phys);

	map_flags = mm_pte_mapping_flags(entry) | PAGE_USER | PAGE_RW;
	map_flags &= ~(PAGE_COW | PAGE_GLOBAL);
	if (mm_pte_executable(entry))
		map_flags |= PAGE_EXEC;

	if (map_page_in_directory(process_pgd(current), vaddr_aligned,
				  new_phys, map_flags) != 0)
	{
		pmm_free_frame(new_phys);
		if (pinned)
			pmm_frame_put(old_phys);
		irq_restore(irq_flags);
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	if (pinned)
		pmm_frame_put(old_phys);
	mm_tlb_sync_page(process_pgd(current), (uintptr_t)vaddr_aligned);
	/* The copied frame may hold code: same rule as a fresh exec mapping. */
	if (map_flags & PAGE_EXEC)
		paging_sync_icache_user(process_pgd(current),
					(uintptr_t)vaddr_aligned, PAGE_SIZE_4KB);
	irq_restore(irq_flags);
	return;
}
