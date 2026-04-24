/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_runner.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Test runner (in-kernel)
 * Estilo KUnit: executor al arranque, resultados por serial.
 * Emite KTAP (Kernel Test Anything Protocol) para parsing estricto.
 */

#include "test/ktest_harness.h"
#include <drivers/serial/serial.h>
#include "process.h"
#include <stddef.h>

int _ktest_failed;
int _ktest_count;
int _ktest_pass;

extern void ktest_boot_ok(void);
extern void ktest_resource_registry(void);
extern void ktest_allocator(void);
extern void ktest_path(void);
extern void ktest_string(void);
extern void ktest_syscall_getpid(void);
extern void ktest_syscall_open_close(void);
extern void ktest_syscall_proc_read(void);
extern void ktest_syscall_pipe(void);
extern void ktest_procfs_uptime(void);
extern void ktest_process_current(void);

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
	ktest_process_current,
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
	"process_current",
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
	1,  /* process_current */
};

static void ktest_print_decimal(uint32_t n)
{
	char buf[12];
	uint32_t i = 0;
	uint32_t d;
	if (n == 0) {
		serial_print("0");
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
	serial_print("1..");
	ktest_print_decimal((uint32_t)total);
	serial_print("\n");

	serial_print("[KTEST] ========================================\n");
	serial_print("[KTEST] IR0 kernel test suite (in-kernel)\n");
	serial_print("[KTEST] ========================================\n");

	for (idx = 0; ktest_functions[idx] != NULL; idx++) {
		_ktest_failed = 0;
		if (ktest_needs_process[idx] && current_process == NULL) {
			serial_print("ok ");
			ktest_print_decimal((uint32_t)(idx + 1));
			serial_print(" - ");
			serial_print(ktest_names[idx] ? ktest_names[idx] : "unknown");
			serial_print(" # SKIP need process\n");
			continue;
		}
		ktest_functions[idx]();
		/* KTAP: ok N - name / not ok N - name */
		if (_ktest_failed) {
			serial_print("not ok ");
			_ktest_pass = 0;
		} else {
			serial_print("ok ");
		}
		ktest_print_decimal((uint32_t)(idx + 1));
		serial_print(" - ");
		serial_print(ktest_names[idx] ? ktest_names[idx] : "unknown");
		serial_print("\n");
	}

	serial_print("[KTEST] ----------------------------------------\n");
	if (_ktest_pass) {
		serial_print("[KTEST] All ");
		ktest_print_decimal((uint32_t)total);
		serial_print(" test(s) passed.\n");
	} else {
		serial_print("[KTEST] Some tests FAILED (total ");
		ktest_print_decimal((uint32_t)total);
		serial_print(").\n");
	}
	serial_print("[KTEST] ========================================\n");
}
