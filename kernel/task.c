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
#include "scheduler.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <arch_interface.h>
#include <string.h>
#include <memory/allocator.h>

// Forward declarations
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

task_t *idle_task = NULL;
static uint32_t next_pid = 1;
static task_t *task_list = NULL;

// Variable global para la tarea actualmente ejecutándose
task_t *current_running_task = NULL;


// Función que ejecuta el idle task - simplemente hace HLT
void idle_task_function(void *arg)
{
    (void)arg; // Evitar warning de parámetro no usado

    // Función simple que solo hace HLT
    cpu_wait();

    // Si llega acá, a hacer noni.
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

    // Setup básico
    task->pid = next_pid++;
    task->priority = priority;
    task->nice = nice;
    task->state = TASK_READY;
    task->stack_base = stack;
    task->stack_size = DEFAULT_STACK_SIZE;
    task->entry = entry;
    task->entry_arg = arg;

    // Setup correcto del stack para x86-64
    uint64_t *stack_ptr = (uint64_t *)((uintptr_t)stack + DEFAULT_STACK_SIZE);

    // Alinear a 16 bytes (ABI x86-64)
    stack_ptr = (uint64_t *)((uintptr_t)stack_ptr & ~0xF);

    // Setup stack frame que switch_context_x64 espera
    *--stack_ptr = 0; // User SS (if needed)
    uint64_t user_rsp = (uint64_t)stack_ptr + 16;
    *--stack_ptr = user_rsp;        // User RSP
    *--stack_ptr = 0x202;           // RFLAGS (IF=1)
    *--stack_ptr = 0x08;            // Kernel CS
    *--stack_ptr = (uint64_t)entry; // RIP - donde saltar

    // El assembly switch_context_x64 restaurará estos registros
    task->rsp = (uint64_t)stack_ptr;
    task->rbp = 0;
    task->rip = (uint64_t)entry; // Punto de entrada
    task->rflags = 0x202;        // Interrupts enabled
    task->cs = 0x08;             // Kernel code segment
    task->ss = 0x10;             // Kernel data segment

    // Agregar a lista global
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

    // Marcar como terminada
    task->state = TASK_TERMINATED;

    // Liberar stack
    if (task->stack_base)
    {
        kfree(task->stack_base);
        task->stack_base = NULL;
    }

    // Remover de lista global
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

    // Liberar estructura
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


// Función de test task que hace algo útil
void test_task_function(void *arg)
{
    int task_id = (int)(uintptr_t)arg;

    print("Test task ");
    print_hex_compact(task_id);
    print(" started\n");

    // Simular trabajo de la tarea
    for (int i = 0; i < 5; i++)
    {
        print("Task ");
        print_hex_compact(task_id);
        print(" iteration ");
        print_hex_compact(i);
        print("\n");

        // Simular trabajo de CPU
        for (volatile int j = 0; j < 1000000; j++)
        {
            // CPU work
        }
    }

    print("Test task ");
    print_hex_compact(task_id);
    print(" completed\n");
}

void create_test_tasks(void)
{
    LOG_OK("Creating test tasks...");

    // Crear idle task primero
    idle_task = create_task(idle_task_function, NULL, 0, 0);
    if (!idle_task)
    {
        panic("Failed to create idle task!");
    }

    // Crear tareas de prueba más interesantes
    task_t *test_task1 = create_task(test_task_function, (void *)1, 1, 0);
    if (!test_task1)
    {
        LOG_WARN("Failed to create test task 1");
    }

    task_t *test_task2 = create_task(test_task_function, (void *)2, 2, 1);
    if (!test_task2)
    {
        LOG_WARN("Failed to create test task 2");
    }

    task_t *test_task3 = create_task(test_task_function, (void *)3, 3, -1);
    if (!test_task3)
    {
        LOG_WARN("Failed to create test task 3");
    }

    // Agregar tareas al scheduler
    add_task(idle_task);
    if (test_task1)
    {
        add_task(test_task1);
    }
    if (test_task2)
    {
        add_task(test_task2);
    }
    if (test_task3)
    {
        add_task(test_task3);
    }

    LOG_OK("Test tasks created successfully");
    delay_ms(2000);
}


task_t *get_task_list(void)
{
    return task_list;
}

uint32_t get_task_count(void)
{
    uint32_t count = 0;
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
