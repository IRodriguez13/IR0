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
#include "debug_bins/dbgshell.h"
#include "rr_sched.h"
#include <drivers/video/typewriter.h>
#include <ir0/kmem.h>
#include <mm/paging.h>
#include <ir0/permissions.h>
#include <string.h>

/*
 * init_1 — Entrada del init de test (debug shell como PID 1).
 * Solo usado cuando KERNEL_DEBUG_SHELL=1.
 */
void init_1(void)
{
	/* Initialize typewriter effect */
	typewriter_init();

	shell_entry();

	/* If shell exits, restart it */
	for (;;)
		shell_entry();
}

/*                                                START INIT DE TEST (debug shell como PID 1)                         */

int start_init_process(void)
{
	process_t *init;

	init = kmalloc(sizeof(process_t));
	if (!init)
		return -1;

	memset(init, 0, sizeof(process_t));

	/* Setup init process */
	init->task.pid = 1;
	init->task.rip = (uint64_t)init_1;
	init->task.rsp = 0x1000000 + 0x1000 - 8;
	init->task.rbp = 0x1000000 + 0x1000;
	init->task.rflags = 0x202;
	init->task.cs = 0x1B;
	init->task.ss = 0x23;
	init->task.ds = 0x23;
	init->task.es = 0x23;
	init->task.fs = 0x23;
	init->task.gs = 0x23;
	init->task.cr3 = create_process_page_directory();

	if (!init->task.cr3)
	{
		kfree(init);
		return -1;
	}

	init->ppid = 1;
	init->state = PROCESS_READY;
	init->stack_start = 0x1000000;
	init->stack_size = 0x1000;
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
	rr_add_process(init);
	rr_schedule_next();

	return 0;
}
