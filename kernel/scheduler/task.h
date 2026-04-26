/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: task.h
 * Description: IR0 kernel source/header file
 */

/* kernel/scheduler/task.h - per-task CPU context and scheduler linkage */
#pragma once
#include <stdint.h>
#include <ir0/types.h>

typedef enum
{
    TASK_READY,      /* Listo para ejecutar */
    TASK_RUNNING,  /* En ejecución */
    TASK_BLOCKED,  /* Esperando E/S, mutex, etc. */
    TASK_TERMINATED
} task_state_t;

typedef struct
{
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x4;
    uint64_t x5;
    uint64_t x6;
    uint64_t x7;
    uint64_t x8;
    uint64_t x9;
    uint64_t x10;
    uint64_t x11;
    uint64_t x12;
    uint64_t x13;
    uint64_t x14;
    uint64_t x15;
    uint64_t x16;
    uint64_t x17;
    uint64_t x18;
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29;
    uint64_t x30;
    uint64_t sp_el0;
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t ttbr0_el1;
} task_arm64_context_t;

/*
 * Proceso / hilo del kernel: registros guardados y metadatos mínimos.
 * La política de planificación usa priority y la lista next.
 */
typedef struct task
{
    uint64_t rax;      /* +0x00 */
    uint64_t rbx;      /* +0x08 */
    uint64_t rcx;      /* +0x10 */
    uint64_t rdx;      /* +0x18 */
    uint64_t rsi;      /* +0x20 */
    uint64_t rdi;      /* +0x28 */
    uint64_t r8;       /* +0x30 */
    uint64_t r9;       /* +0x38 */
    uint64_t r10;      /* +0x40 */
    uint64_t r11;      /* +0x48 */
    uint64_t r12;      /* +0x50 */
    uint64_t r13;      /* +0x58 */
    uint64_t r14;      /* +0x60 */
    uint64_t r15;      /* +0x68 */
    uint64_t rsp;      /* +0x70 */
    uint64_t rbp;      /* +0x78 */
    uint64_t rip;      /* +0x80 */
    uint64_t rflags;   /* +0x88 */
    uint16_t cs;       /* +0x90 */
    uint16_t ds;       /* +0x92 */
    uint16_t es;       /* +0x94 */
    uint16_t fs;       /* +0x96 */
    uint16_t gs;       /* +0x98 */
    uint16_t ss;       /* +0x9A */
    uint16_t padding1; /* +0x9C */
    uint32_t padding2; /* +0x9E */
    uint64_t cr0;      /* +0xA0 */
    uint64_t cr2;      /* +0xA8 */
    uint64_t cr3;      /* +0xB0 */
    uint64_t cr4;      /* +0xB8 */
    uint64_t dr0;      /* +0xC0 */
    uint64_t dr1;      /* +0xC8 */
    uint64_t dr2;      /* +0xD0 */
    uint64_t dr3;      /* +0xD8 */
    uint64_t dr6;      /* +0xE0 */
    uint64_t dr7;      /* +0xE8 */

    /*
     * ARM64 context is stored separately to keep scheduler APIs architecture-neutral.
     * x86-64 fields above are preserved for ABI compatibility with switch_x64.asm.
     */
    task_arm64_context_t arm64;

    pid_t pid;
    uint8_t priority;   /* 0-255, mayor = más prioridad */
    task_state_t state;
    struct task *next;  /* Lista de tareas */

    void *stack_base;
    uint32_t stack_size;
    void (*entry)(void *);
    void *entry_arg;

    uint32_t context_switches;
    uint64_t total_runtime;
    uint64_t last_run_time;

} task_t;

#define MAX_TASKS 256
#define DEFAULT_STACK_SIZE (4 * 1024)

#define TASK_INIT(name, prio)              \
    {                                      \
        .pid = 0,                          \
        .priority = (prio),                \
        .state = TASK_READY,               \
        .context_switches = 0,             \
        .total_runtime = 0,                \
        .next = NULL,                      \
    }

#define task_is_ready(t) ((t)->state == TASK_READY)
#define task_is_running(t) ((t)->state == TASK_RUNNING)
#define task_is_blocked(t) ((t)->state == TASK_BLOCKED)
#define task_is_terminated(t) ((t)->state == TASK_TERMINATED)

task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
void destroy_task(task_t *task);
void task_get_info(task_t *task);
void create_test_tasks(void);

extern task_t *current_running_task;

task_t *get_task_list(void);
pid_t get_task_count(void);
