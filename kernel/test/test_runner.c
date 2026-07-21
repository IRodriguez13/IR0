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
#include "process.h"
#include <stddef.h>

int _ktest_failed;
int _ktest_count;
int _ktest_pass;

static void (*const ktest_functions[])(void) = {
	ktest_boot_ok,
	ktest_resource_registry,
	ktest_allocator,
	ktest_path,
	ktest_string,
	ktest_syscall_getpid,
	ktest_syscall_open_close,
	ktest_syscall_proc_read,
	ktest_syscall_pipe,
	ktest_procfs_uptime,
	ktest_procfs_pid_status,
	ktest_process_current,
	ktest_wait4_status,
	ktest_wait4_specific_reaps_requested_child,
	ktest_wait4_minus_one_reaps_any_child,
	ktest_wait4_specific_no_leak_after_destroy,
	ktest_wait4_wnohang_specific,
	ktest_wait4_wnohang_echild_after_reap,
	ktest_kill_sigterm_wait_status,
	ktest_proc_blockdevices_contract,
	ktest_proc_cpuinfo_contract,
	ktest_proc_version_contract,
	ktest_proc_uptime_contract,
	ktest_sysfs_hostname_contract,
	ktest_proc_netinfo_contract,
	ktest_dev_net_contract,
	ktest_help_sections_contract,
	ktest_mount_proc_contract,
	ktest_mount_tmpfs_contract,
	ktest_mount_multi_fs_contract,
	ktest_mount_longest_prefix_contract,
	ktest_mount_umount_remount_contract,
	ktest_block_hda_read_contract,
	ktest_cred_access_contract,
	ktest_devfs_hci_open_contract,
	ktest_mmap_null_placement,
	ktest_signal_segv_deliver_irq_frame,
	ktest_brk_post_exec,
	ktest_tty_canon_read_immediate,
	ktest_tty_canon_block_wake,
	NULL
};

static const char *const ktest_names[] = {
	"boot_ok",
	"resource_registry",
	"allocator",
	"path",
	"string",
	"syscall_getpid",
	"syscall_open_close",
	"syscall_proc_read",
	"syscall_pipe",
	"procfs_uptime",
	"procfs_pid_status",
	"process_current",
	"wait4_status",
	"wait4_specific_reaps_requested_child",
	"wait4_minus_one_reaps_any_child",
	"wait4_specific_no_leak_after_destroy",
	"wait4_wnohang_specific",
	"wait4_wnohang_echild_after_reap",
	"kill_sigterm_wait_status",
	"proc_blockdevices_contract",
	"proc_cpuinfo_contract",
	"proc_version_contract",
	"proc_uptime_contract",
	"sysfs_hostname_contract",
	"proc_netinfo_contract",
	"dev_net_contract",
	"help_sections_contract",
	"mount_proc_contract",
	"mount_tmpfs_contract",
	"mount_multi_fs_contract",
	"mount_longest_prefix_contract",
	"mount_umount_remount_contract",
	"block_hda_read_contract",
	"cred_access_contract",
	"devfs_hci_open_contract",
	"mmap_null_placement",
	"signal_segv_deliver_irq_frame",
	"brk_post_exec",
	"tty_canon_read_immediate",
	"tty_canon_block_wake",
	NULL
};

/*
 * Tests that require current_process (syscalls, procfs, process_current).
 * Only these are skipped when running at boot before init.
 */
static const int ktest_needs_process[] = {
	0,  /* boot_ok */
	0,  /* resource_registry */
	0,  /* allocator */
	0,  /* path */
	0,  /* string */
	1,  /* syscall_getpid */
	1,  /* syscall_open_close */
	1,  /* syscall_proc_read */
	1,  /* syscall_pipe */
	1,  /* procfs_uptime */
	1,  /* procfs_pid_status */
	1,  /* process_current */
	1,  /* wait4_status */
	1,  /* wait4_specific_reaps_requested_child */
	1,  /* wait4_minus_one_reaps_any_child */
	1,  /* wait4_specific_no_leak_after_destroy */
	1,  /* wait4_wnohang_specific */
	1,  /* wait4_wnohang_echild_after_reap */
	1,  /* kill_sigterm_wait_status */
	1,  /* proc_blockdevices_contract */
	1,  /* proc_cpuinfo_contract */
	1,  /* proc_version_contract */
	1,  /* proc_uptime_contract */
	1,  /* sysfs_hostname_contract */
	1,  /* proc_netinfo_contract */
	1,  /* dev_net_contract */
	1,  /* help_sections_contract */
	1,  /* mount_proc_contract */
	1,  /* mount_tmpfs_contract */
	1,  /* mount_multi_fs_contract */
	1,  /* mount_longest_prefix_contract */
	1,  /* mount_umount_remount_contract */
	1,  /* block_hda_read_contract */
	1,  /* cred_access_contract */
	1,  /* devfs_hci_open_contract */
	1,  /* mmap_null_placement */
	1,  /* signal_segv_deliver_irq_frame */
	1,  /* brk_post_exec */
	1,  /* tty_canon_read_immediate */
	1,  /* tty_canon_block_wake */
};

static void ktest_print_decimal(uint32_t n)
{
	char buf[12];
	uint32_t i = 0;
	uint32_t d;
	if (n == 0) {
		klog_debug("KERN", "0");
		return;
	}
	for (d = n; d; d /= 10)
		buf[i++] = (char)('0' + (d % 10));
	while (i > 0)
		serial_putchar(buf[--i]);
}

void kernel_test_run_all(void)
{
	int total = 0;
	int idx;

	_ktest_count = 0;
	_ktest_pass = 1;

	for (total = 0; ktest_functions[total] != NULL; total++)
		;

	/* KTAP plan: 1..N (obligatorio para parsers) */
	klog_debug("KERN", "1..");
	ktest_print_decimal((uint32_t)total);
	klog_print("\n");

	klog_debug("KERN", "[KTEST] ========================================\n[KTEST] IR0 kernel test suite (in-kernel)\n[KTEST] ========================================\n");

	for (idx = 0; ktest_functions[idx] != NULL; idx++) {
		_ktest_failed = 0;
		if (ktest_needs_process[idx] && current_process == NULL) {
			klog_debug("KERN", "ok ");
			ktest_print_decimal((uint32_t)(idx + 1));
			klog_debug_fmt("KERN", " - %s # SKIP need process\n", ktest_names[idx] ? ktest_names[idx] : "unknown");
			continue;
		}
		ktest_functions[idx]();
		/* KTAP: ok N - name / not ok N - name */
		if (_ktest_failed) {
			klog_debug("KERN", "not ok ");
			_ktest_pass = 0;
		} else {
			klog_debug("KERN", "ok ");
		}
		ktest_print_decimal((uint32_t)(idx + 1));
		klog_debug_fmt("KERN", " - %s", ktest_names[idx] ? ktest_names[idx] : "unknown");
	}

	klog_debug("KERN", "[KTEST] ----------------------------------------\n");
	if (_ktest_pass) {
		klog_debug("KERN", "[KTEST] All ");
		ktest_print_decimal((uint32_t)total);
		klog_debug("KERN", " test(s) passed.\n");
	} else {
		klog_debug("KERN", "[KTEST] Some tests FAILED (total ");
		ktest_print_decimal((uint32_t)total);
		klog_debug("KERN", ").\n");
	}
	klog_debug("KERN", "[KTEST] ========================================\n");
}
