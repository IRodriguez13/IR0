/* SPDX-License-Identifier: GPL-3.0-only */
/* Common TLB-coherence policy; ISAs provide local invalidation mechanics. */

#include <config.h>
#include <ir0/tlb.h>
#include <ir0/tlb_sync.h>

#if defined(CONFIG_SMP) && CONFIG_SMP
#error "TLB sync: remote shootdown backend not implemented"
#endif

static int mm_tlb_root_is_local(address_space_root_t root)
{
	return !root || (uintptr_t)root == mm_current_root();
}

void mm_tlb_sync_page(address_space_root_t root, uintptr_t va)
{
	if (mm_tlb_root_is_local(root))
		tlb_invalidate_page(va);
}

void mm_tlb_sync_all(address_space_root_t root)
{
	if (mm_tlb_root_is_local(root))
		tlb_invalidate_all();
}
