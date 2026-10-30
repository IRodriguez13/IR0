/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_runner.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include "test/ktest_decl.h"
#include <ir0/ktm/klog.h>
#include <ir0/serial_io.h>
#include "process.h"
#include <stddef.h>

int _ktest_failed;
int _ktest_count;
int _ktest_pass;
const char *_ktest_current_name;

/*
 * One row per test keeps the function, KTAP name and process requirement
 * together. The previous parallel arrays had to be edited in lockstep; a
 * missed edit silently shifted names or walked past the function table.
 */
struct ktest_case {
	void (*fn)(void);
	const char *name;
	int needs_process;
};

static const struct ktest_case ktest_cases[] = {
	{ ktest_boot_ok, "boot_ok", 0 },
	{ ktest_resource_registry, "resource_registry", 0 },
	{ ktest_resource_procfs_visibility, "resource_procfs_visibility", 1 },
	{ ktest_allocator, "allocator", 0 },
	{ ktest_path, "path", 0 },
	{ ktest_string, "string", 0 },
	{ ktest_syscall_getpid, "syscall_getpid", 1 },
	{ ktest_syscall_open_close, "syscall_open_close", 1 },
	{ ktest_syscall_proc_read, "syscall_proc_read", 1 },
	{ ktest_syscall_pipe, "syscall_pipe", 1 },
	{ ktest_procfs_uptime, "procfs_uptime", 1 },
	{ ktest_procfs_pid_status, "procfs_pid_status", 1 },
	{ ktest_procfs_pid_maps, "procfs_pid_maps", 1 },
	{ ktest_procfs_pid_fd, "procfs_pid_fd", 1 },
	{ ktest_procfs_pid_environ, "procfs_pid_environ", 1 },
	{ ktest_routed_open_access, "routed_open_access", 1 },
	{ ktest_procfs_self_symlink, "procfs_self_symlink", 1 },
	{ ktest_procfs_pid_exe, "procfs_pid_exe", 1 },
	{ ktest_procfs_stat, "procfs_stat", 1 },
	{ ktest_process_current, "process_current", 1 },
	{ ktest_wait4_status, "wait4_status", 1 },
	{ ktest_wait4_specific_reaps_requested_child,
	  "wait4_specific_reaps_requested_child", 1 },
	{ ktest_wait4_minus_one_reaps_any_child,
	  "wait4_minus_one_reaps_any_child", 1 },
	{ ktest_wait4_specific_no_leak_after_destroy,
	  "wait4_specific_no_leak_after_destroy", 1 },
	{ ktest_wait4_wnohang_specific, "wait4_wnohang_specific", 1 },
	{ ktest_wait4_wnohang_echild_after_reap,
	  "wait4_wnohang_echild_after_reap", 1 },
	{ ktest_wait4_pgrp, "wait4_pgrp", 1 },
	{ ktest_arch_prctl_set_fs_rejects_nonuser,
	  "arch_prctl_set_fs_rejects_nonuser", 1 },
	{ ktest_vfork_complete_clears_link, "vfork_complete_clears_link", 1 },
	{ ktest_kill_sigterm_wait_status, "kill_sigterm_wait_status", 1 },
	{ ktest_proc_blockdevices_contract, "proc_blockdevices_contract", 1 },
	{ ktest_proc_cpuinfo_contract, "proc_cpuinfo_contract", 1 },
	{ ktest_proc_version_contract, "proc_version_contract", 1 },
	{ ktest_proc_uptime_contract, "proc_uptime_contract", 1 },
	{ ktest_sysfs_hostname_contract, "sysfs_hostname_contract", 1 },
	{ ktest_proc_netinfo_contract, "proc_netinfo_contract", 1 },
	{ ktest_dev_net_contract, "dev_net_contract", 1 },
	{ ktest_mount_proc_contract, "mount_proc_contract", 1 },
	{ ktest_mount_tmpfs_contract, "mount_tmpfs_contract", 1 },
	{ ktest_mount_multi_fs_contract, "mount_multi_fs_contract", 1 },
	{ ktest_mount_longest_prefix_contract,
	  "mount_longest_prefix_contract", 1 },
	{ ktest_mount_umount_remount_contract,
	  "mount_umount_remount_contract", 1 },
	{ ktest_block_layer_contract, "block_layer_contract", 1 },
	{ ktest_errno_string_contract, "errno_string_contract", 1 },
	{ ktest_block_hda_read_contract, "block_hda_read_contract", 1 },
	{ ktest_cred_access_contract, "cred_access_contract", 1 },
	{ ktest_devfs_hci_open_contract, "devfs_hci_open_contract", 1 },
	{ ktest_mmap_null_placement, "mmap_null_placement", 1 },
	{ ktest_usercopy_explicit_mm_range_contract,
	  "usercopy_explicit_mm_range_contract", 1 },
	{ ktest_munmap_partial_preserves_vma,
	  "munmap_partial_preserves_vma", 1 },
	{ ktest_mmap_fixed_partial_overlap_preserves_vma,
	  "mmap_fixed_partial_overlap_preserves_vma", 1 },
	{ ktest_mprotect_preserves_cow, "mprotect_preserves_cow", 1 },
	{ ktest_mprotect_updates_maps, "mprotect_updates_maps", 1 },
	{ ktest_reclaim_freed_page_tables_safely,
	  "reclaim_freed_page_tables_safely", 1 },
	{ ktest_signal_segv_deliver_irq_frame,
	  "signal_segv_deliver_irq_frame", 1 },
	{ ktest_brk_post_exec, "brk_post_exec", 1 },
	{ ktest_brk_linux_fail_returns_current,
	  "brk_linux_fail_returns_current", 1 },
	{ ktest_tty_canon_read_immediate, "tty_canon_read_immediate", 1 },
	{ ktest_process_reset_blocked_syscall_state,
	  "process_reset_blocked_syscall_state", 0 },
	{ ktest_sock_stream_accept_fifo_backlog,
	  "sock_stream_accept_fifo_backlog", 0 },
	{ ktest_sock_stream_segmented_queue, "sock_stream_segmented_queue", 0 },
	/*
	 * tty_canon_block_wake: needs cooperative schedule from a real
	 * timer/IRQ context; from kmain+holder it can spin forever.
	 * Covered by smoke-tty / manual; keep out of boot suite.
	 */
};

static void ktest_print_decimal(uint32_t n)
{
	char buf[12];
	uint32_t i = 0;
	uint32_t d;

	if (n == 0) {
		klog_print("0");
		return;
	}
	for (d = n; d; d /= 10)
		buf[i++] = (char)('0' + (d % 10));
	while (i > 0)
		serial_putchar(buf[--i]);
}

int kernel_test_run_all(void)
{
	const int total = (int)(sizeof(ktest_cases) / sizeof(ktest_cases[0]));
	int skipped = 0;
	int idx;

	_ktest_count = 0;
	_ktest_pass = 1;

	/*
	 * KTAP + summary must hit serial at INFO (klog_debug is filtered in
	 * quiet/normal profiles — make kernel-tests greps these strings).
	 */
	klog_print("1..");
	ktest_print_decimal((uint32_t)total);
	klog_print("\n");
	klog_info("KTEST", "IR0 kernel test suite (in-kernel)");

	for (idx = 0; idx < total; idx++) {
		const struct ktest_case *tc = &ktest_cases[idx];

		_ktest_failed = 0;
		if (tc->needs_process && current_process == NULL) {
			skipped++;
			klog_print("ok ");
			ktest_print_decimal((uint32_t)(idx + 1));
			klog_print(" - ");
			klog_print(tc->name ? tc->name : "unknown");
			klog_print(" # SKIP need process\n");
			continue;
		}
		tc->fn();
		/* KTAP: ok N - name / not ok N - name */
		if (_ktest_failed) {
			klog_print("not ok ");
			_ktest_pass = 0;
		} else {
			klog_print("ok ");
		}
		ktest_print_decimal((uint32_t)(idx + 1));
		klog_print(" - ");
		klog_print(tc->name ? tc->name : "unknown");
		klog_print("\n");
	}

	if (_ktest_pass) {
		if (skipped == 0) {
			klog_print("[KTEST] All ");
			ktest_print_decimal((uint32_t)total);
			klog_print(" test(s) passed.\n");
		} else {
			klog_print("[KTEST] ");
			ktest_print_decimal((uint32_t)(total - skipped));
			klog_print(" test(s) passed, ");
			ktest_print_decimal((uint32_t)skipped);
			klog_print(" skipped (total ");
			ktest_print_decimal((uint32_t)total);
			klog_print(").\n");
		}
		klog_info("KTEST", "suite passed");
	} else {
		klog_print("[KTEST] Some tests FAILED (total ");
		ktest_print_decimal((uint32_t)total);
		klog_print(").\n");
		klog_error("KTEST", "suite failed");
	}
	klog_debug("KERN", "[KTEST] ========================================\n");
	return _ktest_pass ? 0 : -1;
}
