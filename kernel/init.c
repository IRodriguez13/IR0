/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: init.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Init de test (PID 1 cuando KERNEL_DEBUG_SHELL=1)
 * Copyright (C) 2025 Iván Rodriguez
 *
 * No es el init real. El init real es /sbin/init, que se carga cuando
 * KERNEL_DEBUG_SHELL=0 en config.h (kexecve("/sbin/init", ...) desde kmain).
 * Este proceso solo existe en modo test/debug y arranca la shell integrada.
 */

#include "process.h"
#include <config.h>
#include "debug_bins/dbgshell.h"
#include "scheduler_api.h"
#include <drivers/video/typewriter.h>
#include <ir0/kmem.h>
#include <mm/paging.h>
#include <ir0/permissions.h>
#include <ir0/errno.h>
#include <fs/vfs.h>
#include <string.h>

/*
 * Build a Unix-like base hierarchy for the debug shell runtime.
 * This is idempotent: existing directories are kept as-is.
 */
static void init_prepare_unix_hierarchy(void)
{
	const char *dirs[] = {
		"/bin",
		"/sbin",
		"/etc",
		"/etc/init.d",
		"/usr",
		"/usr/bin",
		"/usr/sbin",
		"/usr/lib",
		"/usr/include",
		"/var",
		"/var/log",
		"/var/tmp",
		"/var/run",
		"/home",
		"/root",
		"/tmp",
		"/opt",
		"/boot",
		"/srv",
		"/run",
		"/mnt",
		"/mnt/tmp",
		"/mnt/simple",
		"/mnt/fat",
		NULL
	};

	for (int i = 0; dirs[i] != NULL; i++)
	{
		int ret = vfs_mkdir(dirs[i], 0755);
		if (ret != 0 && ret != -EEXIST)
			break;
	}
}

/*
 * init_1 — Entrada del init de test (debug shell como PID 1).
 * Solo usado cuando KERNEL_DEBUG_SHELL=1.
 */
void init_1(void)
{
	/*
	 * If test runner is linked, execute ktests here (PID 1) so
	 * current_process-dependent tests run in real process context.
	 */
	{
		extern void kernel_test_run_all(void) __attribute__((weak));
		if (kernel_test_run_all)
			kernel_test_run_all();
	}

	/* Initialize typewriter effect */
	typewriter_init();
	init_prepare_unix_hierarchy();

	shell_entry();

	/* If shell exits, restart it */
	for (;;)
		shell_entry();
}

/*                                                START INIT DE TEST (debug shell como PID 1)                         */

int start_init_process(void)
{
	process_t *init;
	int have_identity_low_map = 0;

	init = kmalloc(sizeof(process_t));
	if (!init)
		return -1;

	memset(init, 0, sizeof(process_t));

	/* Setup init process */
	init->task.pid = 1;
	init->task.rip = (uint64_t)init_1;
	/*
	 * Stack 16 KiB at identity-mapped kernel region.
	 * Debug shell commands accumulate local buffers (ls, rm, etc.).
	 */
	init->task.rsp = INIT_DEBUG_STACK_BASE + USER_STACK_SIZE - 16;
	init->task.rbp = init->task.rsp;
	init->task.rflags = RFLAGS_IF;
	/*
	 * Kernel selectors: the debug shell executes kernel code directly
	 * (init_1 → shell_entry). Ring 0 is required because boot page tables
	 * are supervisor-only (no User bit).
	 */
	init->task.cs = KERNEL_CODE_SEL;
	init->task.ss = KERNEL_DATA_SEL;
	init->task.ds = KERNEL_DATA_SEL;
	init->task.es = KERNEL_DATA_SEL;
	init->task.fs = KERNEL_DATA_SEL;
	init->task.gs = KERNEL_DATA_SEL;
	init->task.cr3 = create_process_page_directory();

	if (!init->task.cr3)
	{
		kfree(init);
		return -1;
	}

	/*
	 * Ensure low identity mappings are available in init CR3.
	 * Debug shell stack/call path relies on INIT_DEBUG_STACK_BASE.
	 */
	{
		uint64_t *new_pml4 = (uint64_t *)init->task.cr3;
		uint64_t *cur_pml4 = (uint64_t *)get_current_page_directory();

		if (cur_pml4 && (cur_pml4[0] & PAGE_PRESENT))
		{
			new_pml4[0] = cur_pml4[0];
			have_identity_low_map = 1;
		}
	}

	/*
	 * Map explicit 4KB pages only if low identity mapping was not inherited.
	 * If PML4[0] exists, low memory may be mapped with 2MB huge pages and
	 * forcing 4KB mappings can fail even though the mapping is valid.
	 */
	if (!have_identity_low_map)
	{
		for (uint64_t off = 0; off < USER_STACK_SIZE; off += 0x1000)
		{
			uint64_t va = INIT_DEBUG_STACK_BASE + off;
			if (map_page_in_directory((uint64_t *)init->task.cr3, va, va,
						  PAGE_PRESENT | PAGE_RW) != 0)
			{
				kfree_aligned((void *)init->task.cr3);
				kfree(init);
				return -1;
			}
		}
	}

	/* PID 1 is the session leader; parent of init is the kernel (no userspace parent). */
	init->ppid = 0;
	init->mode = KERNEL_MODE;
	init->state = PROCESS_READY;
	init->stack_start = INIT_DEBUG_STACK_BASE;
	init->stack_size = USER_STACK_SIZE;
	init->page_directory = (uint64_t *)init->task.cr3;

	/* Initialize current working directory */
	strncpy(init->cwd, "/", sizeof(init->cwd) - 1);
	init->cwd[sizeof(init->cwd) - 1] = '\0';
	
	/* Set process command name */
	strncpy(init->comm, "debshell", sizeof(init->comm) - 1);
	init->comm[sizeof(init->comm) - 1] = '\0';

	/* Set root permissions for dbgshell */
	init->uid = ROOT_UID;
	init->gid = ROOT_GID;
	init->euid = ROOT_UID;
	init->egid = ROOT_GID;
	init->umask = DEFAULT_UMASK;

	/* Initialize fd table (stdin/stdout/stderr) */
	process_init_fd_table(init);

	/* Add to scheduler and start */
	sched_add_process(init);
	sched_schedule_next();

	return 0;
}
