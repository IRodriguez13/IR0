// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: task.c
 * Description: Task management implementation for process scheduling
 */

#include "task.h"
#include <ir0/vga.h>
#include <ir0/oops.h>
#include <arch_interface.h>
#include <string.h>
#include <mm/allocator.h>
#include <ir0/kmem.h>

/* =============================================================================== */
/* GLOBAL VARIABLES */
/* =============================================================================== */

task_t *idle_task = NULL;
static pid_t next_pid = 1;
static task_t *task_list = NULL;

/* Global variable for currently running task */
task_t *current_running_task = NULL;


/* Function that runs the idle task - simply executes HLT */
void idle_task_function(void *arg)
{
    (void)arg; /* Avoid unused parameter warning */

    /* Simple function that just executes HLT */
    cpu_wait();

    /* If we get here, do nothing (should not happen) */
    cpu_relax();
}

task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice)
{
    task_t *task = (task_t *)kmalloc(sizeof(task_t));
    if (!task)
        return NULL;

    void *stack = kmalloc(DEFAULT_STACK_SIZE);
    if (!stack)
    {
        kfree(task);
        return NULL;
    }

    memset(task, 0, sizeof(task_t));

    /* Basic setup */
    task->pid = next_pid++;
    task->priority = priority;
    task->nice = nice;
    task->state = TASK_READY;
    task->stack_base = stack;
    task->stack_size = DEFAULT_STACK_SIZE;
    task->entry = entry;
    task->entry_arg = arg;

    /* Correct stack setup for x86-64 */
    uint64_t *stack_ptr = (uint64_t *)((uintptr_t)stack + DEFAULT_STACK_SIZE);

    /* Align to 16 bytes (x86-64 ABI) */
    stack_ptr = (uint64_t *)((uintptr_t)stack_ptr & ~0xF);

    /* Setup stack frame that switch_context_x64 expects */
    *--stack_ptr = 0; /* User SS (if needed) */
    uint64_t user_rsp = (uint64_t)stack_ptr + 16;
    *--stack_ptr = user_rsp;        /* User RSP */
    *--stack_ptr = 0x202;           /* RFLAGS (IF=1) */
    *--stack_ptr = 0x08;            /* Kernel CS */
    *--stack_ptr = (uint64_t)entry; /* RIP - jump target */

    /* Assembly switch_context_x64 will restore these registers */
    task->rsp = (uint64_t)stack_ptr;
    task->rbp = 0;
    task->rip = (uint64_t)entry; /* Entry point */
    task->rflags = 0x202;        /* Interrupts enabled */
    task->cs = 0x08;             /* Kernel code segment */
    task->ss = 0x10;             /* Kernel data segment */

    /* Add to global list */
    task->next = task_list;
    task_list = task;

    return task;
}

void destroy_task(task_t *task)
{
    if (!task)
    {
        return;
    }

    /* Mark as terminated */
    task->state = TASK_TERMINATED;

    /* Free stack */
    if (task->stack_base)
    {
        kfree(task->stack_base);
        task->stack_base = NULL;
    }

    /* Remove from global list */
    if (task_list == task)
    {
        task_list = task->next;
    }
    else
    {
        task_t *current = task_list;
        while (current && current->next != task)
        {
            current = current->next;
        }
        if (current)
        {
            current->next = task->next;
        }
    }

    /* Free structure */
    kfree(task);
}

void task_set_nice(task_t *task, int8_t nice)
{
    if (!task)
    {
        return;
    }

    if (nice < MIN_NICE || nice > MAX_NICE)
    {
        LOG_WARN("task_set_nice: Invalid nice value");
        return;
    }

    task->nice = nice;
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

    print("  Nice: ");
    print_hex_compact(task->nice);
    print("\n");
}


/* Test task function that does something useful */
void test_task_function(void *arg)
{
    int task_id = (int)(uintptr_t)arg;

    print("Test task ");
    print_hex_compact(task_id);
    print(" started\n");

    /* Simular trabajo de la tarea */
    for (int i = 0; i < 5; i++)
    {
        print("Task ");
        print_hex_compact(task_id);
        print(" iteration ");
        print_hex_compact(i);
        print("\n");

        /* Simular trabajo de CPU */
        for (volatile int j = 0; j < 1000000; j++)
        {
            /* CPU work */
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
        {
            count++;
        }
        current = current->next;
    }

    return count;
}
