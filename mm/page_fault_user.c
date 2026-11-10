/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault_user.c
 * Description: User faults: VMA lookup, anonymous zero pages, not-present and read-protection faults.
 */

#include <ir0/cpu.h>
#include <ir0/tlb_sync.h>
#include <ir0/mm_alloc.h>
#include <ir0/pmm.h>
#include <ir0/oom.h>
#include <ir0/paging.h>
#include <ir0/abi/mmap_contract.h>
#include <ir0/swap.h>
#include "page_fault_internal.h"

int pf_addr_in_heap(process_t *p, uint64_t fa)
{
	uint64_t heap_lo;
	uint64_t heap_hi;

	if (!p)
		return 0;

	heap_lo = (uint64_t)process_heap_start(p);
	heap_hi = (uint64_t)process_heap_end(p);
	if (heap_hi <= heap_lo)
		return 0;
	/*
	 * heap_start==0 with a live brk end: do NOT snap to USER_HEAP_BASE
	 * (0x20000000) — that excluded the low brk image and skipped identity
	 * promote (write-fault no-COW pte=…063 !USER at ~0x9fb000).
	 */
	if (heap_lo == 0)
		heap_lo = mm_user_va_start();

	return (fa >= heap_lo && fa < heap_hi);
}

int pf_addr_in_stack(process_t *p, uint64_t fa)
{
	uint64_t lo;
	uint64_t hi;

	if (!p)
		return 0;

	lo = (uint64_t)process_stack_start(p);
	hi = lo + (uint64_t)process_stack_size(p);
	if (lo == 0 || hi <= lo)
	{
		lo = (uint64_t)USER_STACK_TOP - (uint64_t)USER_STACK_SIZE;
		hi = (uint64_t)USER_STACK_TOP;
	}
	return (fa >= lo && fa < hi);
}

struct mmap_region *pf_mmap_region_for(process_t *p, uint64_t fa)
{
	struct mmap_region *r;

	if (!p)
		return NULL;
	for (r = process_mmap_list(p); r != NULL; r = r->next)
	{
		uintptr_t base = (uintptr_t)r->addr;
		uint64_t end = base + (uint64_t)r->length;

		if (fa >= (uint64_t)base && fa < end)
			return r;
	}
	return NULL;
}

/*
 * Linux do_anonymous_page only inside a VMA. IR0 also sees present+!USER
 * identity leaves after a 2MiB PDE split (sibling slots stay PA==VA).
 */
int pf_addr_may_promote_identity(process_t *p, uint64_t fa)
{
	uint64_t heap_hi;

	if (!p)
		return 0;
	if (pf_addr_in_heap(p, fa) || pf_addr_in_stack(p, fa) ||
	    pf_mmap_region_for(p, fa) != NULL)
		return 1;
	/*
	 * Brk cursor may lag heap_start bookkeeping; identity below heap_end
	 * in the supervisor window is still a heap hole (Linux pte_none).
	 */
	heap_hi = (uint64_t)process_heap_end(p);
	if (heap_hi > PF_IDENTITY_USER_FLOOR &&
	    fa >= PF_IDENTITY_USER_FLOOR && fa < heap_hi &&
	    fa < (uint64_t)USER_MMAP_START)
		return 1;
	return 0;
}

/*
 * Linux do_anonymous_page (mm/memory.c): install a private zeroed USER leaf.
 * Heap, stack, and anonymous mmap VMAs share this path for !present faults.
 */
void pf_demand_zero_page(process_t *current, uint64_t fault_addr,
				uint64_t map_flags, uint64_t *stack,
				const struct page_fault_info *info)
{
	uintptr_t phys_addr;
	uint64_t vaddr_aligned;

	phys_addr = mm_alloc_frame(MM_ALLOC_ATOMIC);
	if (phys_addr == 0)
	{
		/*
		 * Linux __alloc_pages_slowpath: reclaim, then at most one kill,
		 * then one retry. Return 0 means a page was freed or another
		 * task was killed. Killing this task does not return here.
		 */
		if (oom_handle_alloc_failure(current, "demand-zero") != 0)
		{
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
		phys_addr = mm_alloc_frame(MM_ALLOC_ATOMIC);
		if (phys_addr == 0)
		{
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
	}

	vaddr_aligned = fault_addr & ~0xFFFUL;
	if (map_page_in_directory(process_pgd(current), vaddr_aligned,
				  phys_addr, map_flags) != 0)
	{
		pmm_free_frame(phys_addr);
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	paging_zero_phys_page(phys_addr);
}

/*
 * IR0 artifact: the process address-space root keeps a supervisor 2 MiB
 * identity map for PMM access.
 * After fork, heap/stack holes remain present+!USER (Linux would be pte_none).
 * Treat as do_anonymous_page — never memcpy identity (PMM garbage → musl abort).
 * Refs: Linux do_anonymous_page; Gorman ch.4 demand paging.
 *
 * Returns 0 on success, -1 on OOM / map failure (caller delivers SEGV).
 */
int pf_identity_to_anon_zero(process_t *current, uint64_t vaddr_aligned,
				    uint64_t map_flags)
{
	uintptr_t new_phys;

	new_phys = mm_alloc_frame(MM_ALLOC_ATOMIC);
	if (!new_phys)
	{
		/* Same retry contract as do_anonymous_page. */
		if (oom_handle_alloc_failure(current, "identity-promote") != 0)
			return -1;
		new_phys = mm_alloc_frame(MM_ALLOC_ATOMIC);
		if (!new_phys)
			return -1;
	}
	paging_zero_phys_page(new_phys);
	if (map_page_in_directory(process_pgd(current), vaddr_aligned, new_phys,
				  map_flags) != 0)
	{
		pmm_free_frame(new_phys);
		return -1;
	}
	mm_tlb_sync_page(process_pgd(current), (uintptr_t)vaddr_aligned);
	return 0;
}

/* User access to a page with no valid descriptor (demand paging, swap-in). */
void pf_user_not_present(const struct page_fault_info *info, uint64_t *stack,
			 uint64_t fault_addr)
{
	process_t *current;
	int write = info->write;
	int insn_fetch = info->exec;

	if (!mm_user_va_ok((uintptr_t)fault_addr, 1))
	{
		current = process_get_current();
		if (current)
			pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	current = process_get_current();
	if (!current || !process_pgd(current))
		return;

	{
		uint64_t *swap_pte;
		uint64_t map_flags = PAGE_USER | PAGE_RW;
		struct mmap_region *swap_mr;

		swap_pte = paging_get_pte(process_pgd(current),
					  (uintptr_t)(fault_addr & ~0xFFFULL));
		if (swap_pte && mm_pte_is_swap(*swap_pte))
		{
			swap_mr = pf_mmap_region_for(current, fault_addr);
			if (swap_mr)
			{
				map_flags = PAGE_USER;
				if (swap_mr->prot & 0x2)
					map_flags |= PAGE_RW;
				if (swap_mr->prot & 0x4)
					map_flags |= PAGE_EXEC;
			}
			if (ir0_swap_in_page(current,
					     (uintptr_t)fault_addr,
					     map_flags) == 0)
				return;
			pf_user_segv(current, stack, fault_addr, info);
			return;
		}
	}

	{
		struct mmap_region *mr;

		mr = pf_mmap_region_for(current, fault_addr);
		if (mr != NULL)
		{
			if ((mr->flags & IR0_MAP_ANONYMOUS) == 0)
			{
				pf_user_segv(current, stack, fault_addr, info);
				return;
			}
			if (mr->prot == 0)
			{
				pf_user_segv(current, stack, fault_addr, info);
				return;
			}
			if (write && (mr->prot & 0x2) == 0) /* PROT_WRITE */
			{
				pf_user_segv(current, stack, fault_addr, info);
				return;
			}
			if (insn_fetch && (mr->prot & 0x4) == 0) /* PROT_EXEC */
			{
				pf_user_segv(current, stack, fault_addr, info);
				return;
			}
			{
				uint64_t map_flags = PAGE_USER;

				if (mr->prot & 0x2) /* PROT_WRITE */
					map_flags |= PAGE_RW;
				if (mr->prot & 0x4) /* PROT_EXEC */
					map_flags |= PAGE_EXEC;
				pf_demand_zero_page(current, fault_addr, map_flags,
						    stack, info);
			}
			return;
		}
	}

	if (!pf_addr_in_heap(current, fault_addr) &&
	    !pf_addr_in_stack(current, fault_addr))
	{
		pf_user_segv(current, stack, fault_addr, info);
		return;
	}

	{
		uint64_t map_flags = PAGE_USER | PAGE_RW;

		if (insn_fetch)
			map_flags |= PAGE_EXEC;
		pf_demand_zero_page(current, fault_addr, map_flags, stack, info);
	}
	return;
}

/*
 * Read fault on present supervisor leaf in a user VMA — same as write:
 * Linux do_anonymous_page (zero), not memcpy of identity PMM.
 */
int pf_user_read_present(const struct page_fault_info *info, uint64_t *stack,
			 uint64_t fault_addr)
{
	process_t *current;

	uint64_t *pte;
	uint64_t vaddr_aligned;
	unsigned long irq_flags;

	current = process_get_current();
	if (!current || !process_pgd(current))
		return 1;

	vaddr_aligned = fault_addr & ~0xFFFUL;
	irq_flags = irq_save();
	if (paging_ensure_4k_leaf(process_pgd(current),
				  vaddr_aligned) != 0)
	{
		irq_restore(irq_flags);
		pf_user_segv(current, stack, fault_addr, info);
		return 1;
	}
	pte = paging_get_pte(process_pgd(current), vaddr_aligned);
	if (!pte || !pte_present(*pte))
	{
		irq_restore(irq_flags);
		if (pf_addr_may_promote_identity(current, fault_addr))
		{
			pf_demand_zero_page(current, fault_addr,
					    PAGE_USER | PAGE_RW, stack,
					    info);
			return 1;
		}
		pf_user_segv(current, stack, fault_addr, info);
		return 1;
	}
	if (pte && pte_present(*pte) && !pte_user(*pte) &&
	    pf_addr_may_promote_identity(current, fault_addr))
	{
		if (pf_identity_to_anon_zero(current, vaddr_aligned,
					     PAGE_USER | PAGE_RW) != 0)
		{
			irq_restore(irq_flags);
			pf_user_segv(current, stack, fault_addr, info);
			return 1;
		}
		irq_restore(irq_flags);
		return 1;
	}
	irq_restore(irq_flags);
	return 0;
}
