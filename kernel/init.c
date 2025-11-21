/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Init Process (PID 1)
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 * 
 * First userspace process, runs the shell
 */

#include "process.h"
#include "shell.h"
#include "rr_sched.h"
#include <drivers/video/typewriter.h>
#include <ir0/memory/kmem.h>
#include <ir0/memory/paging.h>
#include <string.h>



void init_1(void)
{
	/* Initialize typewriter effect */
	typewriter_init();

	shell_entry();

	/* If shell exits, restart it */
	for (;;)
		shell_entry();
}

/*                                                START INIT 1 PROCESS                                                         */

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
	strcpy(init->cwd, "/");
	
	/* Set process command name */
	strncpy(init->comm, "init_1", sizeof(init->comm) - 1);
	init->comm[sizeof(init->comm) - 1] = '\0';

	/* Add to scheduler and start */
	rr_add_process(init);
	rr_schedule_next();

	return 0;
}
