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
#include "mmu_early.h"
#include "syscall_early.h"
#include <ir0/cpu_info.h>
#include <ir0/copy_user.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/arch_mm.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_log.h>
#include <ir0/errno.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/product_path.h>
#include <ir0/idle.h>
#include <ir0/mm_struct.h>
#include <ir0/oops.h>
#include <ir0/process_exit.h>
#include <ir0/cwd.h>
#include <ir0/fd_install.h>
#include <ir0/vfs_dispatch.h>
#include <ir0/vfs_file.h>
#include <ir0/vfs.h>
#include <ir0/open_flags.h>
#include <fs/tmpfs.h>
#include <ir0/syscall_identity.h>
#include <ir0/tlb.h>
#include <ir0/paging.h>
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
static process_t g_child;
static process_t g_idle;
static uint8_t g_idle_stack[4096] __attribute__((aligned(16)));
static int g_product_usercopy_observed;

static int arm64_product_copy_from_user(void *destination, uint64_t source,
					uint64_t size)
{
	int ret = copy_from_user(destination, (const void *)(uintptr_t)source,
				 (size_t)size);

	if (ret == 0 && !g_product_usercopy_observed)
	{
		g_product_usercopy_observed = 1;
		ir0_boot_smoke("ARM64_PRODUCT_USERCOPY_OK");
	}
	return ret;
}

static int arm64_product_copy_to_user(uint64_t destination, const void *source,
				      uint64_t size)
{
	int ret = copy_to_user((void *)(uintptr_t)destination, source,
			       (size_t)size);

	if (ret == 0 && !g_product_usercopy_observed)
	{
		g_product_usercopy_observed = 1;
		ir0_boot_smoke("ARM64_PRODUCT_USERCOPY_OK");
	}
	return ret;
}

static const struct early_user_services g_product_usercopy = {
	.copy_from_user = arm64_product_copy_from_user,
	.copy_to_user = arm64_product_copy_to_user,
};

static uintptr_t g_owned_user_root;

static int arm64_owned_mm_retire_contract(void)
{
	process_t probe;
	mm_struct_t *mm;
	address_space_root_t root;
	uint8_t *bytes = (uint8_t *)&probe;
	size_t i;

	for (i = 0; i < sizeof(probe); i++)
		bytes[i] = 0;
	root = mm_create_user_root();
	if (!root)
		return -1;
	mm = mm_create();
	if (!mm)
	{
		mm_destroy_user_root(root);
		return -1;
	}
	mm_init_root(mm, root, 1);
	process_mm_bind(&probe, mm);
	if (!process_mm_owns_tables(&probe))
		return -1;
	mm_put(mm);
	probe.mm = NULL;
	return arm64_mmu_root_owned((uint64_t)(uintptr_t)root) ? -1 : 0;
}

static void arm64_idle_quiescent(void)
{
	process_t *init = process_find_by_pid(1);

	if (init && process_is_zombie(init))
	{
		if (!g_guest.mm && g_owned_user_root &&
		    mm_current_root() == paging_kernel_address_space() &&
		    !arm64_mmu_root_owned(g_owned_user_root))
			ir0_boot_smoke("ARM64_PROCESS_MM_EXIT_OK");
		ir0_boot_smoke("ARM64_COMMON_IDLE_OK");
		ir0_boot_smoke("ARM64_SYSCALL_OK");
		ir0_boot_smoke("ARM64_EL0_RET_OK");
		arm64_psci_system_off();
	}
	cpu_idle();
}

void arm64_common_prepare_final_el0(void)
{
	paging_pin_kernel_address_space((uintptr_t)arm64_mmu_root_a());
	if (!paging_kernel_address_space() ||
	    paging_kernel_address_space() == mm_current_root())
	{
		ir0_boot_smoke("ARM64_PROCESS_PGD_FAIL");
		panic("arm64: kernel root is missing or is the live user root");
	}
	if (process_mm_adopt_active(&g_guest, 1) != 0 ||
	    !process_pgd(&g_guest) ||
	    (uintptr_t)process_pgd(&g_guest) != mm_current_root() ||
	    !process_mm_owns_tables(&g_guest))
	{
		ir0_boot_smoke("ARM64_PROCESS_PGD_FAIL");
		panic("arm64: EL0 process failed to adopt active address space");
	}
	g_owned_user_root = (uintptr_t)process_pgd(&g_guest);
	ir0_boot_smoke("ARM64_PROCESS_PGD_OK");
	ir0_boot_smoke("ARM64_PROCESS_MM_HEAP_OK");
	ir0_boot_smoke("ARM64_PROCESS_MM_OWNED_OK");
	if (arm64_owned_mm_retire_contract() != 0)
		panic("arm64: owned mm retirement contract failed");
	ir0_boot_smoke("ARM64_PROCESS_MM_RETIRE_OK");
	arm64_syscall_usercopy_bind(&g_product_usercopy);
	sched_reset();
	process_guest_rearm(&g_guest);
	sched_adopt_running_context(&g_guest);
	early_process_arm_schedule_exit();
}

static int arm64_product_read(struct vfs_file *f, void *buf, size_t n)
{
	return vfs_read(f, (char *)buf, n);
}

static int arm64_product_write(struct vfs_file *f, const void *buf, size_t n)
{
	return vfs_write(f, (const char *)buf, n);
}

void arm64_common_prepare_fdtable(void)
{
	const struct ir0_fd_install_ops *ops = ir0_fd_install_product();

	process_init_fd_table(&g_guest);
	process_init_fd_table(&g_child);
	if (!files_struct_live(g_guest.files) ||
	    !files_struct_live(g_child.files) || !ops)
		return;
	early_rootfs_bind_fdtable(ops);
	early_process_bind_files(ops);
	early_rootfs_bind_vfs_ops();
	ir0_boot_smoke("ARM64_COMMON_FDTABLE_OK");
	if (early_rootfs_vfs_file_used())
		ir0_boot_smoke("ARM64_COMMON_VFS_FILE_OK");
	if (vfs_dispatch_bound())
		ir0_boot_smoke("ARM64_COMMON_VFS_OPS_OK");
	{
		struct vfs_file probe = {
			.path = "/dev/zero",
			.pos = 0,
			.flags = 0,
			.ref_count = 1,
		};
		uint8_t z[4];
		int n;

		n = vfs_dispatch_read(&probe, z, sizeof(z));
		if (n == 4 && z[0] == 0 && z[1] == 0 && z[2] == 0 && z[3] == 0 &&
		    probe.pos == (off_t)sizeof(z))
			ir0_boot_smoke("ARM64_COMMON_VFS_OPS_READ_OK");
	}
	if (tmpfs_register() == 0 &&
	    vfs_mount("none", "/tmp", "tmpfs") == 0)
	{
		struct vfs_file *f = NULL;
		char got[4];
		int n;

		if (vfs_open("/tmp/product", IR0_O_RDWR | IR0_O_CREAT, 0644,
			     &f) == 0 && f &&
		    vfs_write(f, "IR0", 3) == 3 &&
		    vfs_lseek(f, 0, 0) == 0 &&
		    vfs_read(f, got, 3) == 3 &&
		    got[0] == 'I' && got[1] == 'R' && got[2] == '0')
			ir0_boot_smoke("ARM64_PRODUCT_VFS_OK");
		if (f)
			(void)vfs_file_put(f);
		{
			static const struct ir0_product_path_ops path_ops = {
				.stat = vfs_stat,
				.open = vfs_open,
				.read = arm64_product_read,
				.write = arm64_product_write,
				.lseek = vfs_lseek,
				.put = vfs_file_put,
				.mkdir = vfs_mkdir,
				.unlink = vfs_unlink,
				.rmdir = vfs_rmdir,
				.truncate = vfs_truncate,
			};

			early_rootfs_bind_product_path(&path_ops);
			if (vfs_mkdir("/tmp/fold", 0755) == 0 &&
			    ir0_product_path_is_tmp("/tmp/fold"))
				ir0_boot_smoke("ARM64_EARLY_PATH_FOLD_OK");
		}
	}
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
	process_set_current(&child);
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
		process_set_current(saved_current);
		process_list = saved_process_list;
		return -1;
	}
	sched_remove_process(&parent);
	process_set_current(saved_current);
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
		bytes = (uint8_t *)&g_child;
		for (i = 0; i < sizeof(g_child); i++)
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
		g_guest.cwd[0] = '/';
		g_guest.cwd[1] = 0;
		g_guest.root[0] = '/';
		g_guest.root[1] = 0;
		g_child.task.pid = 2;
		g_child.ppid = 1;
		g_child.lifecycle = PROCESS_LIFECYCLE_ALIVE;
		g_child.state = PROCESS_READY;
		g_child.mode = USER_MODE;
		g_child.comm[0] = 'c';
		g_child.comm[1] = 'l';
		g_child.comm[2] = 'o';
		g_child.comm[3] = 'n';
		g_child.comm[4] = 'e';
		g_child.comm[5] = 0;
		g_child.cwd[0] = '/';
		g_child.cwd[1] = 0;
		g_child.root[0] = '/';
		g_child.root[1] = 0;
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
		g_idle.next = NULL;
		g_guest.next = NULL;
		g_child.next = NULL;
		process_registry_reset();
		process_registry_insert(&g_idle);
		process_registry_insert(&g_guest);
		process_registry_insert(&g_child);
		process_exit_bind_services(&guest_ops);
		process_idle_bind(&idle_ops);
		process_guest_install(&g_guest);
		if (process_find_by_pid(0) == &g_idle &&
		    process_find_by_pid(1) == &g_guest &&
		    process_find_by_pid(2) == &g_child &&
		    process_get_current() == &g_guest)
			ir0_boot_smoke("ARM64_PROCESS_REGISTRY_OK");
		early_process_bind_common_exit(process_exit_guest);
		early_process_bind_schedule_exit(process_exit);
		early_process_bind_identity(ir0_syscall_identity_product());
		arm64_syscall_bind_identity_provider(&ir0_product_identity_provider);
		ir0_boot_smoke("ARM64_PRODUCT_IDENTITY_OK");
		early_rootfs_bind_cwd(ir0_cwd_product());
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
