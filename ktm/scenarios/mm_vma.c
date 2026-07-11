/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_vma.c
 * Description: KTM scenario — mmap_region list insert/count/teardown (VMA bookkeeping).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

static unsigned vma_count(struct mmap_region *head)
{
	unsigned n = 0;

	for (; head; head = head->next)
		n++;
	return n;
}

static void vma_free_list(struct mmap_region *head)
{
	while (head)
	{
		struct mmap_region *next = head->next;

		kfree(head);
		head = next;
	}
}

static struct mmap_region *vma_push(struct mmap_region *head, void *addr,
				    size_t length)
{
	struct mmap_region *r = kmalloc_try(sizeof(*r));

	if (!r)
		return NULL;
	memset(r, 0, sizeof(*r));
	r->addr = addr;
	r->hint_addr = addr;
	r->length = length;
	r->prot = 0x3; /* PROT_READ|PROT_WRITE */
	r->flags = 0x20; /* MAP_ANONYMOUS-ish */
	r->next = head;
	return r;
}

static int scenario_mm_vma_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	struct mmap_region *list = NULL;
	struct mmap_region *cloned = NULL;
	struct mmap_region *walk;
	struct mmap_region *tail = NULL;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	KTM_CHECKPOINT(KTM_CP_MM_MAP);
	list = vma_push(list, (void *)0x09000000UL, 4096);
	KTM_REQUIRE(list != NULL);
	list = vma_push(list, (void *)0x09010000UL, 8192);
	KTM_REQUIRE(list != NULL);
	KTM_V1_ASSERT_TRUE(vma_count(list) == 2);

	/* Shallow clone of the list (fork mmap_list intent). */
	for (walk = list; walk; walk = walk->next)
	{
		struct mmap_region *node = kmalloc_try(sizeof(*node));

		KTM_REQUIRE(node != NULL);
		*node = *walk;
		node->next = NULL;
		if (!cloned)
			cloned = node;
		else
			tail->next = node;
		tail = node;
	}
	KTM_V1_ASSERT_TRUE(vma_count(cloned) == 2);
	KTM_V1_ASSERT_TRUE(cloned != list);
	KTM_V1_ASSERT_TRUE(cloned->addr == list->addr);
	KTM_V1_ASSERT_TRUE(cloned->length == list->length);

	KTM_CHECKPOINT(KTM_CP_MM_UNMAP);
	vma_free_list(cloned);
	vma_free_list(list);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("mm.frames", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_mm_vma = {
	.name = "mm.vma",
	.flags = 0,
	.setup = NULL,
	.run = scenario_mm_vma_run,
	.teardown = NULL,
};

void ktm_scenario_register_mm_vma(void)
{
	(void)ktm_scenario_register(&scenario_mm_vma);
}
