/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * ARCH-5 host contract: arch_mm_va_indices + PTE encode/decode.
 */

#include "test_harness.h"
#include <stddef.h>
#include <stdint.h>

void arch_mm_va_indices(uintptr_t va, size_t idx[4]);
int arch_mm_pte_present(uint64_t e);
int arch_mm_pte_large(uint64_t e);
uintptr_t arch_mm_pte_phys(uint64_t e);
uint64_t arch_mm_make_table_pte(uintptr_t phys, int user);
uint64_t arch_mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec);
void arch_mm_pte_set_user(uint64_t *e);

void test_arch_mm_pte_facade(void)
{
	size_t idx[4];
	uint64_t table;
	uint64_t leaf;
	uint64_t e;

	TEST_BEGIN("arch_mm_pte_facade_indices_encode");

	arch_mm_va_indices((uintptr_t)0x00007f1234567000ULL, idx);
	ASSERT_EQ(0xFEUL, (unsigned long)idx[0]);
	ASSERT_EQ(0x48UL, (unsigned long)idx[1]);
	ASSERT_EQ(0x1A2UL, (unsigned long)idx[2]);
	ASSERT_EQ(0x167UL, (unsigned long)idx[3]);

	table = arch_mm_make_table_pte(0x2000UL, 1);
	ASSERT(arch_mm_pte_present(table));
	ASSERT(!arch_mm_pte_large(table));
	ASSERT_EQ(0x2000UL, (unsigned long)arch_mm_pte_phys(table));
	ASSERT(table & 0x4ULL); /* USER */

	leaf = arch_mm_make_leaf_pte(0x3000UL, 0x2ULL, 0);
	ASSERT(arch_mm_pte_present(leaf));
	ASSERT(leaf & (1ULL << 63)); /* NX */
	ASSERT_EQ(0x3000UL, (unsigned long)arch_mm_pte_phys(leaf));

	leaf = arch_mm_make_leaf_pte(0x4000UL, 0x2ULL, 1);
	ASSERT(!(leaf & (1ULL << 63)));

	e = arch_mm_make_table_pte(0x5000UL, 0);
	ASSERT(!(e & 0x4ULL));
	arch_mm_pte_set_user(&e);
	ASSERT(e & 0x4ULL);

	TEST_END();
}
