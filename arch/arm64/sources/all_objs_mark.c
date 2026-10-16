/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: all_objs_mark.c
 * Description: Marker TU forced into kernel-arm64-all.bin link.
 */

#include "pl011.h"
#include "exc_early.h"
#include <ir0/cpu_info.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/arch_mm.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_log.h>
#include <ir0/errno.h>
#include <ir0/early_process.h>
#include <ir0/idle.h>
#include <ir0/process_exit.h>
#include <ir0/sched.h>
#include <ir0/signals.h>
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
	int runnable_before;
	int wait_status;
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
	if (process_lifecycle_prepare_reap(&child) != -EBUSY)
		return -1;
	process_lifecycle_publish_exit(&child, 42);
	if (!process_is_zombie(&child) ||
	    !process_lifecycle_child_matches(&parent, &child, (pid_t)-1) ||
	    process_child_wait_status_word(&child) != (42 << 8) ||
	    process_lifecycle_prepare_reap(&child) != 0)
		return -1;
	child.exit_signal = SIGKILL;
	process_lifecycle_publish_exit(&child, 99);
	if (child.exit_code != 0 ||
	    process_child_wait_status_word(&child) != SIGKILL)
		return -1;
	process_lifecycle_publish_reaped(&child);
	if (child.lifecycle != PROCESS_LIFECYCLE_DEAD ||
	    process_lifecycle_prepare_reap(&child) != -EALREADY)
		return -1;

	init.task.pid = 1;
	init.lifecycle = PROCESS_LIFECYCLE_ALIVE;
	init.state = PROCESS_RUNNING;
	parent.task.pid = 7;
	orphan.task.pid = 8;
	orphan.ppid = parent.task.pid;
	orphan.lifecycle = PROCESS_LIFECYCLE_ZOMBIE;
	orphan.state = PROCESS_ZOMBIE;
	orphan.exit_code = 7;
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
	if (process_wait_prepare_child_wake(&parent, &init, &wait_status) != -ECHILD) {
		process_list = saved_process_list;
		return -1;
	}
	if (!process_wait_child_matches_blocked_target(&parent, orphan.task.pid)) {
		process_list = saved_process_list;
		return -1;
	}
	if (process_wait_prepare_child_wake(&parent, &orphan, &wait_status) != 0 ||
	    wait_status != (7 << 8)) {
		process_list = saved_process_list;
		return -1;
	}
	runnable_before = sched_count_runnable();
	process_wait_wake_blocked_parent(&parent, &orphan);
	if (parent.state != PROCESS_READY ||
	    sched_count_runnable() != runnable_before + 1) {
		process_list = saved_process_list;
		return -1;
	}
	sched_remove_process(&parent);
	if (sched_count_runnable() != runnable_before) {
		process_list = saved_process_list;
		return -1;
	}
	process_reparent_children(&parent);
	if (orphan.ppid != init.task.pid ||
	    process_reap_detach_zombie(&orphan) != 0 ||
	    process_find_by_pid(orphan.task.pid) != NULL ||
	    orphan.lifecycle != PROCESS_LIFECYCLE_DEAD) {
		process_list = saved_process_list;
		return -1;
	}
	process_list = saved_process_list;
	return 0;
}

static process_t g_guest;
static process_t g_idle;
static uint8_t g_idle_stack[4096] __attribute__((aligned(16)));

static void arm64_idle_quiescent(void)
{
	process_t *init = process_find_by_pid(1);

	if (init && process_is_zombie(init))
	{
		ir0_boot_smoke("ARM64_COMMON_IDLE_OK");
		ir0_boot_smoke("ARM64_SYSCALL_OK");
		ir0_boot_smoke("ARM64_EL0_RET_OK");
		arm64_psci_system_off();
	}
	cpu_idle();
}

void arm64_common_prepare_final_el0(void)
{
	sched_reset();
	process_guest_rearm(&g_guest);
	sched_adopt_running_context(&g_guest);
	early_process_arm_schedule_exit();
}

static int arm64_exit_notify_signal(int pid, int sig)
{
	process_t *target = process_find_by_pid(pid);

	if (!target)
		return -ESRCH;
	target->signal_pending |= SIGNAL_MASK(sig);
	return 0;
}

static int arm64_common_exit_contract(void)
{
	static process_t parent;
	static process_t child;
	static process_t init;
	static const struct process_exit_services ops = {
		.notify_signal = arm64_exit_notify_signal,
	};
	process_t *saved_process_list;
	process_t *saved_current;
	uint8_t *bytes;
	size_t i;
	int runnable_before;

	if (!process_exit || !process_pre_zombie_teardown)
		return -1;

	bytes = (uint8_t *)&parent;
	for (i = 0; i < sizeof(parent); i++)
		bytes[i] = 0;
	bytes = (uint8_t *)&child;
	for (i = 0; i < sizeof(child); i++)
		bytes[i] = 0;
	bytes = (uint8_t *)&init;
	for (i = 0; i < sizeof(init); i++)
		bytes[i] = 0;

	process_exit_bind_services(&ops);
	init.task.pid = 1;
	init.lifecycle = PROCESS_LIFECYCLE_ALIVE;
	init.state = PROCESS_RUNNING;
	parent.task.pid = 10;
	parent.lifecycle = PROCESS_LIFECYCLE_ALIVE;
	parent.state = PROCESS_BLOCKED;
	child.task.pid = 11;
	child.ppid = 10;
	child.lifecycle = PROCESS_LIFECYCLE_ALIVE;
	child.state = PROCESS_RUNNING;
	init.next = &parent;
	parent.next = &child;
	child.next = NULL;
	saved_process_list = process_list;
	saved_current = current_process;
	process_list = &init;
	current_process = &child;
	process_wait_state_arm(&parent, child.task.pid, 0, NULL);
	runnable_before = sched_count_runnable();
	process_pre_zombie_teardown(&child);
	process_lifecycle_publish_exit(&child, 3);
	process_notify_parent_of_exit(&child);
	sched_remove_process(&child);
	if (!process_is_zombie(&child) ||
	    process_child_wait_status_word(&child) != (3 << 8) ||
	    parent.state != PROCESS_READY ||
	    (parent.signal_pending & SIGNAL_MASK(SIGCHLD)) == 0 ||
	    sched_count_runnable() != runnable_before + 1)
	{
		current_process = saved_current;
		process_list = saved_process_list;
		return -1;
	}
	sched_remove_process(&parent);
	current_process = saved_current;
	process_list = saved_process_list;
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
	if (arm64_common_exit_contract() == 0)
		ir0_boot_smoke("ARM64_COMMON_EXIT_OK");
	else
		ir0_boot_smoke("ARM64_COMMON_EXIT_FAIL");
	{
		static const struct process_exit_services guest_ops = {
			.notify_signal = arm64_exit_notify_signal,
		};
		static const struct process_idle_services idle_ops = {
			.on_quiescent = arm64_idle_quiescent,
		};
		uint8_t *bytes = (uint8_t *)&g_guest;
		size_t i;
		uintptr_t sp;

		for (i = 0; i < sizeof(g_guest); i++)
			bytes[i] = 0;
		bytes = (uint8_t *)&g_idle;
		for (i = 0; i < sizeof(g_idle); i++)
			bytes[i] = 0;
		g_guest.task.pid = 1;
		g_guest.ppid = 0;
		g_guest.lifecycle = PROCESS_LIFECYCLE_ALIVE;
		g_guest.state = PROCESS_RUNNING;
		g_guest.mode = USER_MODE;
		g_guest.comm[0] = 'i';
		g_guest.comm[1] = 'n';
		g_guest.comm[2] = 'i';
		g_guest.comm[3] = 't';
		g_guest.comm[4] = 0;
		g_idle.task.pid = 0;
		g_idle.lifecycle = PROCESS_LIFECYCLE_ALIVE;
		g_idle.state = PROCESS_READY;
		g_idle.mode = KERNEL_MODE;
		g_idle.comm[0] = 'i';
		g_idle.comm[1] = 'd';
		g_idle.comm[2] = 'l';
		g_idle.comm[3] = 'e';
		g_idle.comm[4] = 0;
		sp = (uintptr_t)g_idle_stack + sizeof(g_idle_stack);
		sp &= ~(uintptr_t)0xf;
		g_idle.task.arch.sp_el0 = sp;
		g_idle.task.arch.x30 = (uint64_t)(uintptr_t)kernel_idle_loop;
		g_idle.next = &g_guest;
		g_guest.next = NULL;
		process_list = &g_idle;
		process_exit_bind_services(&guest_ops);
		process_idle_bind(&idle_ops);
		process_guest_install(&g_guest);
		early_process_bind_common_exit(process_exit_guest);
		early_process_bind_schedule_exit(process_exit);
		ir0_boot_smoke("ARM64_COMMON_GUEST_OK");
		ir0_boot_smoke("ARM64_COMMON_IDLE_READY");
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
