/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Init Process (PID 1)
 * Copyright (C) 2025 Iván Rodriguez
 *
 * First userspace process, runs the shell
 */

#include "process.h"
#include "shell.h"
#include "rr_sched.h"
#include <ir0/memory/kmem.h>
#include <ir0/memory/paging.h>
#include <string.h>

/* ========================================================================== */
/* INIT PROCESS ENTRY POINT                                                   */
/* ========================================================================== */

void init_1(void)
{
	shell_entry();
	
	/* If shell exits, restart it */
	for (;;)
		shell_entry();
}

/* ========================================================================== */
/* START INIT PROCESS                                                         */
/* ========================================================================== */

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

	/* Add to scheduler and start */
	rr_add_process(init);
	rr_schedule_next();

	return 0;
}
