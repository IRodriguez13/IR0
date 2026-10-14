/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: all_objs_mark.c
 * Description: Marker TU forced into kernel-arm64-all.bin link.
 */

#include "pl011.h"
#include <ir0/arch_mm.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_log.h>
#include <ir0/errno.h>
#include <kernel/process.h>

static int arm64_mm_descriptor_contract(void)
{
	uint64_t leaf;
	uint64_t flags;

	leaf = mm_make_leaf_pte(0x12345000UL,
				IR0_MM_MAP_USER | IR0_MM_MAP_WRITE, 1);
	flags = mm_pte_mapping_flags(leaf);
	if (mm_pte_phys(leaf) != 0x12345000UL ||
	    (flags & (IR0_MM_MAP_PRESENT | IR0_MM_MAP_USER |
		      IR0_MM_MAP_WRITE | IR0_MM_MAP_EXEC)) !=
		(IR0_MM_MAP_PRESENT | IR0_MM_MAP_USER |
		 IR0_MM_MAP_WRITE | IR0_MM_MAP_EXEC))
		return -1;

	mm_pte_mark_cow(&leaf);
	flags = mm_pte_mapping_flags(leaf);
	if ((flags & IR0_MM_MAP_WRITE) || !(flags & IR0_MM_MAP_COW) ||
	    !(flags & IR0_MM_MAP_USER))
		return -1;

	leaf = mm_make_leaf_pte(0x12346000UL, IR0_MM_MAP_USER, 0);
	if (mm_pte_executable(leaf) ||
	    (mm_pte_mapping_flags(leaf) & IR0_MM_MAP_EXEC))
		return -1;

	return 0;
}

int arm64_common_root_smoke(void)
{
	address_space_root_t root = mm_create_user_root();

	if (!root)
		return -1;
	mm_destroy_user_root(root);
	return 0;
}

static int arm64_boot_memory_contract(void)
{
	struct ir0_phys_range ranges[IR0_BOOT_MEMORY_MAX_BANKS];
	size_t count;
	size_t i;

	int ret = boot_memory_get_usable_ranges(ranges,
						 IR0_BOOT_MEMORY_MAX_BANKS, &count);

	if (ret != 0)
		return ret;
	for (i = 0; i < count; i++)
	{
		if ((ranges[i].base & 0xFFFU) != 0U ||
		    (ranges[i].size & 0xFFFU) != 0U || ranges[i].size == 0U ||
		    ranges[i].base > UINT64_MAX - ranges[i].size)
			return -1;
	}
	return 0;
}

static int arm64_common_lifecycle_contract(void)
{
	static process_t parent;
	static process_t child;
	static process_t orphan;
	static process_t init;
	process_t *saved_process_list;
	uint8_t *bytes;
	size_t i;

	bytes = (uint8_t *)&parent;
	for (i = 0; i < sizeof(parent); i++)
		bytes[i] = 0;
	bytes = (uint8_t *)&child;
	for (i = 0; i < sizeof(child); i++)
		bytes[i] = 0;
	bytes = (uint8_t *)&orphan;
	for (i = 0; i < sizeof(orphan); i++)
		bytes[i] = 0;
	bytes = (uint8_t *)&init;
	for (i = 0; i < sizeof(init); i++)
		bytes[i] = 0;

	parent.task.pid = 1;
	parent.pgid = 1;
	child.task.pid = 2;
	child.ppid = 1;
	child.pgid = 1;
	child.lifecycle = PROCESS_LIFECYCLE_ALIVE;
	child.state = PROCESS_RUNNING;
	process_lifecycle_publish_exit(&child, 42);
	if (!process_is_zombie(&child) ||
	    !process_lifecycle_child_matches(&parent, &child, (pid_t)-1) ||
	    process_child_wait_status_word(&child) != (42 << 8))
		return -1;
	process_lifecycle_publish_reaped(&child);
	if (child.lifecycle != PROCESS_LIFECYCLE_DEAD)
		return -1;

	init.task.pid = 1;
	init.lifecycle = PROCESS_LIFECYCLE_ALIVE;
	init.state = PROCESS_RUNNING;
	parent.task.pid = 7;
	orphan.task.pid = 8;
	orphan.ppid = parent.task.pid;
	orphan.lifecycle = PROCESS_LIFECYCLE_ZOMBIE;
	orphan.state = PROCESS_ZOMBIE;
	init.next = &parent;
	parent.next = &orphan;
	orphan.next = NULL;
	saved_process_list = process_list;
	process_list = &init;
	if (!process_has_zombie_child(&parent)) {
		process_list = saved_process_list;
		return -1;
	}
	process_wait_state_arm(&parent, orphan.task.pid, 0, NULL);
	if (!process_wait_child_matches_blocked_target(&parent, orphan.task.pid)) {
		process_list = saved_process_list;
		return -1;
	}
	process_reparent_children(&parent);
	process_list = saved_process_list;
	if (orphan.ppid != init.task.pid)
		return -1;
	return 0;
}

void arm64_all_objs_mark(void)
{
	ir0_boot_smoke("ARM64_ALL_OBJS_LINK_OK");
	if (arm64_mm_descriptor_contract() == 0)
		ir0_boot_smoke("ARM64_MM_DESCRIPTOR_CONTRACT_OK");
	else
		ir0_boot_smoke("ARM64_MM_DESCRIPTOR_CONTRACT_FAIL");
	if (arm64_common_lifecycle_contract() == 0)
	{
		ir0_boot_smoke("ARM64_COMMON_LIFECYCLE_OK");
		ir0_boot_smoke("ARM64_COMMON_WAIT_OK");
	}
	else
	{
		ir0_boot_smoke("ARM64_COMMON_LIFECYCLE_FAIL");
		ir0_boot_smoke("ARM64_COMMON_WAIT_FAIL");
	}
	{
		int inventory = arm64_boot_memory_contract();

		if (inventory == 0)
			ir0_boot_smoke("ARM64_BOOT_MEMORY_OK");
		else if (inventory == -ENODEV)
			ir0_boot_smoke("ARM64_BOOT_MEMORY_UNAVAILABLE");
		else
			ir0_boot_smoke("ARM64_BOOT_MEMORY_FAIL");
	}
}
