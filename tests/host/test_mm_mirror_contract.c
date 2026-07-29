/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_mm_mirror_contract.c
 * Description: mm_struct sole-store contract — address space lives in mm only.
 */

#include "test_harness_ir0.h"
#include <stdint.h>
#include <string.h>

typedef struct
{
	uint64_t *page_directory;
	struct
	{
		char *pad;
	} *mmap_list;
	uint64_t mmap_base;
	uint64_t heap_start;
	uint64_t heap_end;
	uint64_t stack_start;
	uint64_t stack_size;
} mm_stub_t;

typedef struct
{
	mm_stub_t *mm;
} proc_stub_t;

static uint64_t *pgd_of(const proc_stub_t *p)
{
	return (p && p->mm) ? p->mm->page_directory : NULL;
}

static uint64_t heap_end_of(const proc_stub_t *p)
{
	return (p && p->mm) ? p->mm->heap_end : 0;
}

void test_mm_mirror_contract(void)
{
	mm_stub_t mm;
	proc_stub_t proc;
	uint64_t pgd;

	memset(&mm, 0, sizeof(mm));
	memset(&proc, 0, sizeof(proc));
	mm.page_directory = &pgd;
	mm.heap_start = 0x2000;
	mm.heap_end = 0x3000;
	mm.stack_start = 0x700000;
	mm.stack_size = 0x8000;
	proc.mm = &mm;

	TEST_BEGIN("mm_sole_store_via_mm");
	ASSERT(pgd_of(&proc) == &pgd);
	ASSERT(heap_end_of(&proc) == 0x3000);
	TEST_END();

	TEST_BEGIN("mm_sole_store_null_without_mm");
	proc.mm = NULL;
	ASSERT(pgd_of(&proc) == NULL);
	ASSERT(heap_end_of(&proc) == 0);
	TEST_END();

	TEST_BEGIN("mm_sole_store_heap_mutation");
	proc.mm = &mm;
	mm.heap_end = 0x4000;
	ASSERT(heap_end_of(&proc) == 0x4000);
	TEST_END();
}
