/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * ARCH-5 host contract: mm_va_indices + PTE encode/decode.
 */

#include "test_harness.h"
#include <stddef.h>
#include <stdint.h>

void mm_va_indices(uintptr_t va, size_t idx[4]);
int mm_pte_present(uint64_t e);
int mm_pte_large(uint64_t e);
uintptr_t mm_pte_phys(uint64_t e);
uint64_t mm_make_table_pte(uintptr_t phys, int user);
uint64_t mm_make_leaf_pte(uintptr_t phys, uint64_t flags12, int exec);
void mm_pte_set_user(uint64_t *e);

void test_arch_mm_pte_facade(void)
{
	size_t idx[4];
	uint64_t table;
	uint64_t leaf;
	uint64_t e;

	TEST_BEGIN("arch_mm_pte_facade_indices_encode");

	mm_va_indices((uintptr_t)0x00007f1234567000ULL, idx);
	ASSERT_EQ(0xFEUL, (unsigned long)idx[0]);
	ASSERT_EQ(0x48UL, (unsigned long)idx[1]);
	ASSERT_EQ(0x1A2UL, (unsigned long)idx[2]);
	ASSERT_EQ(0x167UL, (unsigned long)idx[3]);

	table = mm_make_table_pte(0x2000UL, 1);
	ASSERT(mm_pte_present(table));
	ASSERT(!mm_pte_large(table));
	ASSERT_EQ(0x2000UL, (unsigned long)mm_pte_phys(table));
	ASSERT(table & 0x4ULL); /* USER */

	leaf = mm_make_leaf_pte(0x3000UL, 0x2ULL, 0);
	ASSERT(mm_pte_present(leaf));
	ASSERT(leaf & (1ULL << 63)); /* NX */
	ASSERT_EQ(0x3000UL, (unsigned long)mm_pte_phys(leaf));

	leaf = mm_make_leaf_pte(0x4000UL, 0x2ULL, 1);
	ASSERT(!(leaf & (1ULL << 63)));

	e = mm_make_table_pte(0x5000UL, 0);
	ASSERT(!(e & 0x4ULL));
	mm_pte_set_user(&e);
	ASSERT(e & 0x4ULL);

	TEST_END();
}
