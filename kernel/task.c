/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: task.c
 * Description: Portable task create/destroy; kernel stack frame via arch hook.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "task.h"
#include <config.h>
#include <ir0/vga.h>
#include <ir0/oops.h>
#include <ir0/cpu.h>
#include <ir0/arch_syscall_frame.h>
#include <string.h>
#include <mm/allocator.h>
#include <ir0/kmem.h>

task_t *idle_task = NULL;
static pid_t next_pid = 1;
static task_t *task_list = NULL;

task_t *current_running_task = NULL;

void idle_task_function(void *arg)
{
	(void)arg;
	cpu_halt();
	cpu_relax();
}

task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice)
{
	task_t *task = (task_t *)kmalloc(sizeof(task_t));
	void *stack;

	if (!task)
		return NULL;

	stack = kmalloc(DEFAULT_STACK_SIZE);
	if (!stack)
	{
		kfree(task);
		return NULL;
	}

	memset(task, 0, sizeof(task_t));

	task->pid = next_pid++;
	task->priority = priority;
	(void)nice;
	task->state = TASK_READY;
	task->stack_base = stack;
	task->stack_size = DEFAULT_STACK_SIZE;
	task->entry = entry;
	task->entry_arg = arg;

	if (arch_task_setup_kernel_stack(task, stack, DEFAULT_STACK_SIZE, entry) != 0)
	{
		kfree(stack);
		kfree(task);
		return NULL;
	}

	task->next = task_list;
	task_list = task;

	return task;
}

void destroy_task(task_t *task)
{
	if (!task)
		return;

	task->state = TASK_TERMINATED;

	if (task->stack_base)
	{
		kfree(task->stack_base);
		task->stack_base = NULL;
	}

	if (task_list == task)
	{
		task_list = task->next;
	}
	else
	{
		task_t *current = task_list;

		while (current && current->next != task)
			current = current->next;
		if (current)
			current->next = task->next;
	}

	kfree(task);
}

void task_get_info(task_t *task)
{
	if (!task)
	{
		LOG_ERR("task_get_info: task is NULL");
		return;
	}

	print("Task Info:\n");
	print("  PID: ");
	print_hex_compact(task->pid);
	print("\n");

	print("  State: ");
	switch (task->state)
	{
	case TASK_READY:
		print("READY");
		break;
	case TASK_RUNNING:
		print("RUNNING");
		break;
	case TASK_BLOCKED:
		print("BLOCKED");
		break;
	case TASK_TERMINATED:
		print("TERMINATED");
		break;
	default:
		print("UNKNOWN");
		break;
	}
	print("\n");

	print("  Priority: ");
	print_hex_compact(task->priority);
	print("\n");
}

void test_task_function(void *arg)
{
	int task_id = (int)(uintptr_t)arg;
	int i;

	print("Test task ");
	print_hex_compact(task_id);
	print(" started\n");

	for (i = 0; i < 5; i++)
	{
		volatile int j;

		print("Task ");
		print_hex_compact(task_id);
		print(" iteration ");
		print_hex_compact(i);
		print("\n");

		for (j = 0; j < 1000000; j++)
		{
		}
	}

	print("Test task ");
	print_hex_compact(task_id);
	print(" completed\n");
}

task_t *get_task_list(void)
{
	return task_list;
}

pid_t get_task_count(void)
{
	pid_t count = 0;
	task_t *current = task_list;

	while (current)
	{
		if (current->state != TASK_TERMINATED)
			count++;
		current = current->next;
	}

	return count;
}
