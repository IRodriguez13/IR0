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

#include "test_harness.h"
#include <stdio.h>

int _ir0_test_failed;
int _ir0_test_count;
int _ir0_test_pass;

extern void test_harness_smoke(void);
extern void test_example_asserts(void);
extern void test_pseudo_fs_register_lookup_read(void);
extern void test_pseudo_fs_proc_registry_paths(void);
extern void test_pseudo_fs_path_has_children_and_collect(void);
extern void test_usb_host_disabled_controller_count(void);
extern void test_bt_scan_sync_stub(void);
extern void test_vfs_backend_contract(void);
extern void test_stat_user_abi(void);
extern void test_named_fifo_supervise(void);
extern void test_path_resolve_at(void);
extern void test_ktm_poll_arch_resume_matrix(void);
extern void test_musl_mmap_contract(void);
extern void test_signal_rt_sigaction_abi(void);
extern void test_musl_cred_abi(void);

static void (*test_functions[])(void) = {
	test_harness_smoke,
	test_example_asserts,
	test_pseudo_fs_register_lookup_read,
	test_pseudo_fs_proc_registry_paths,
	test_pseudo_fs_path_has_children_and_collect,
	test_usb_host_disabled_controller_count,
	test_bt_scan_sync_stub,
	test_vfs_backend_contract,
	test_stat_user_abi,
	test_named_fifo_supervise,
	test_path_resolve_at,
	test_ktm_poll_arch_resume_matrix,
	test_musl_mmap_contract,
	test_signal_rt_sigaction_abi,
	test_musl_cred_abi,
	NULL
};

int main(void)
{
	_ir0_test_count = 0;
	_ir0_test_pass = 1;

	fprintf(stderr, "[TEST] IR0 kernel test suite\n");
	fprintf(stderr, "[TEST] --------------------\n");

	int n = 0;
	for (int i = 0; test_functions[i] != NULL; i++) {
		test_functions[i]();
		n++;
	}
	_ir0_test_count = n;  /* Asegurar total para TEST_EXIT */
	TEST_EXIT();
	return 1;
}
