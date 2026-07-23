/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * Host stubs for mm_va_indices / pte_* / make_* (ARCH-5 Pack D).
 * Mirrors x86-64 4K PTE bit layout used by arch/x86-64/sources/mm_ops.c.
 */

#include <stddef.h>
#include <stdint.h>

#define X86_PTE_PRESENT   0x1ULL
#define X86_PTE_RW        0x2ULL
#define X86_PTE_USER      0x4ULL
#define X86_PTE_LARGE     0x80ULL
#define X86_PTE_NX        (1ULL << 63)
#define X86_PTE_PFN_MASK  0x000FFFFFFFFFF000ULL
#define X86_INDEX_MASK    0x1FFUL

void mm_va_indices(uintptr_t va, size_t idx[4])
{
	idx[0] = ((uint64_t)va >> 39) & X86_INDEX_MASK;
	idx[1] = ((uint64_t)va >> 30) & X86_INDEX_MASK;
	idx[2] = ((uint64_t)va >> 21) & X86_INDEX_MASK;
	idx[3] = ((uint64_t)va >> 12) & X86_INDEX_MASK;
}

int mm_pte_present(uint64_t e)
{
	return (e & X86_PTE_PRESENT) != 0;
}

int mm_pte_large(uint64_t e)
{
	return mm_pte_present(e) && ((e & X86_PTE_LARGE) != 0);
}

uintptr_t mm_pte_phys(uint64_t e)
{
	return (uintptr_t)(e & X86_PTE_PFN_MASK);
}

uint64_t mm_make_table_pte(uintptr_t phys, int user)
{
	uint64_t e = ((uint64_t)phys & X86_PTE_PFN_MASK) | X86_PTE_PRESENT | X86_PTE_RW;

	if (user)
		e |= X86_PTE_USER;
	return e;
}

uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec)
{
	uint64_t e = ((uint64_t)phys & X86_PTE_PFN_MASK) | (flags12 & 0xFFFULL) |
		     X86_PTE_PRESENT;

	if (!exec)
		e |= X86_PTE_NX;
	return e;
}

void mm_pte_set_user(uint64_t *e)
{
	if (e)
		*e |= X86_PTE_USER;
}
